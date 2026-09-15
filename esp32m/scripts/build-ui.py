import os
import re
import sys
import enum
import glob
import gzip
import json
import base64
import shutil
import string
import asyncio
import hashlib
import logging
import argparse
import subprocess
from packaging import version

ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')

# Package name prefixes that are always kept as "*" in project package.json
# because they are resolved via yarn link to the local core installation.
_INTERNAL_SCOPES = ('@esp32m/', '@ts-libs/')

_LOCK_ENTRY_RE  = re.compile(r'^(?:"([^"]+)"|([^":]+)):\s*$')
_LOCK_VERSION_RE = re.compile(r'^\s{2}version:\s+(\S+)\s*$')

def _version_key(ver_str):
    try:
        return tuple(int(x) for x in ver_str.split('.')[:3])
    except (ValueError, AttributeError):
        return (0, 0, 0)

def _parse_yarn_lock(lockfile_path):
    """Parse a Yarn v3+ lock file and return {pkg_name: resolved_version}."""
    versions = {}
    try:
        with open(lockfile_path, 'r', encoding='utf-8', errors='replace') as f:
            lines = f.readlines()
    except IOError:
        return versions
    i = 0
    while i < len(lines):
        m = _LOCK_ENTRY_RE.match(lines[i])
        if m:
            # Each spec in the header looks like:  pkg@npm:range  or  @scope/pkg@npm:range
            pkg_names = set()
            entry = m.group(1) or m.group(2)
            for spec in entry.split(', '):
                for registry in ('@npm:', '@workspace:'):
                    if registry in spec:
                        name = spec.split(registry)[0].strip('"')
                        if not any(name.startswith(s) for s in _INTERNAL_SCOPES):
                            pkg_names.add(name)
                        break
            # Find the resolved version in the indented block that follows
            j = i + 1
            resolved = None
            while j < len(lines) and (not lines[j].strip() or lines[j][0] == ' '):
                vm = _LOCK_VERSION_RE.match(lines[j])
                if vm:
                    resolved = vm.group(1)
                    break
                j += 1
            if resolved and pkg_names:
                for name in pkg_names:
                    existing = versions.get(name)
                    if existing is None or _version_key(resolved) > _version_key(existing):
                        versions[name] = resolved
        i += 1
    return versions

# this is to strip non-ascii chars from yarn output because ESP-IDF doesn't seem to like it
async def strip_non_ascii(input_stream, output_stream):
    ascii_chars = set(string.printable)
    while not input_stream.at_eof():
        output = await input_stream.readline()
        output=ansi_escape.sub('', output.decode())
        output=''.join(filter(lambda x: x in ascii_chars, output))
        output_stream.buffer.write(output.encode("ascii"))
        output_stream.flush()

class BuildMode(enum.IntEnum):
    Full = 0
    Once = 1
    Never = 2

class Yarn:
    def __init__(self, dir):
        self.dir=dir
        yarn = shutil.which("yarn")
        if not yarn:
            raise Exception(
                "yarn not found, please install, more information here: https://yarnpkg.com/getting-started/install "
            )
        logging.info(f"using yarn executable {yarn}")
        ver = subprocess.check_output([yarn, '-v'], cwd=self.dir).decode()
        if not ver:
            raise Exception("yarn did not respond to version request, check yarn installation")
        minver=version.parse("3.2.2")
        if version.parse(ver) < minver:
            raise Exception(f"yarn version {minver} or later expected, got {ver}")
        self.yarn=yarn
       
    def link(self, target):
        logging.info(f"linking {self.dir} to {target}")
        # subprocess.run([self.yarn, 'link', target, '-A'], cwd=self.dir)
        self.run([self.yarn, 'link', target, '-A'])
        return

    def build(self):
        logging.info(f"building {self.dir}")
        # subprocess.run([self.yarn, 'build'], cwd=self.dir)
        self.run([self.yarn, 'build'])
        return

    async def run_async(self, command):
        process = await asyncio.create_subprocess_exec(
            *command, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE, cwd=self.dir
        )
        await asyncio.gather(
            strip_non_ascii(process.stderr, sys.stderr),
            strip_non_ascii(process.stdout, sys.stdout),
        )
        await process.communicate()
        if process.returncode:
            raise subprocess.CalledProcessError(process.returncode, command)

    def run(self, command):
        asyncio.run(self.run_async(command))
    

class PackageJson:
    def __init__(self, path):
        self.path=path
        self.json=None
    def exists(self):
        return os.path.exists(self.path)
    def ensureLoaded(self):
        if not os.path.exists(self.path):
            raise Exception(f"file not found: {self.path}")
        if not self.json:
            with open(self.path, 'r') as f:
                self.json = json.load(f)
    def syncVersions(self, versions):
        """Replace dependency versions with those from the core package map.
        Skips internal @esp32m/* and @ts-libs/* packages (resolved via yarn link).
        Returns True if any version was changed."""
        self.ensureLoaded()
        changed = False
        for section in ('dependencies', 'devDependencies'):
            for name in self.json.get(section, {}):
                if any(name.startswith(s) for s in _INTERNAL_SCOPES):
                    continue
                if name in versions and self.json[section][name] != versions[name]:
                    self.json[section][name] = versions[name]
                    changed = True
        return changed
    def removeResolutions(self, versions=None):
        self.ensureLoaded()
        if "resolutions" in self.json:
            del self.json["resolutions"]
        # The resolutions field forces specific versions across the entire dependency
        # tree, preventing webpack from bundling multiple copies of the same package.
        tracked = [
            "react", "react-dom", "lodash",
            "webpack", "webpack-cli", "webpack-dev-server",
            "@mui/icons-material", "@mui/material", "@mui/system", "@mui/utils",
        ]
        self.json["resolutions"] = {pkg: (versions or {}).get(pkg, "*") for pkg in tracked}
    def removeTypeModule(self):
        self.ensureLoaded()
        if self.json.get("type") == "module":
            del self.json["type"]
    def dump(self, indent=4):
        if not self.json:
            raise Exception("nothing to dump")
        with open(self.path, 'w') as f:
            json.dump(self.json, f, indent=indent)

class Package:
    def __init__(self, dir):
        if not os.path.exists(dir):
            raise Exception(f"package directory not found: {dir}")
        self.dir=dir
        self.packageJson=PackageJson(os.path.join(dir, "package.json"))
        self.distDir=os.path.join(self.dir, "dist")
    
class Esp32m:
    def __init__(self, dir):
        self.package=Package(os.path.join(dir, "web-ui"))

    def collectVersions(self):
        """Collect all 3rd-party dependency version constraints from the core web-ui
        packages. Returns a dict mapping package name to version constraint string."""
        versions = {}
        pattern = os.path.join(self.package.dir, '**', 'package.json')
        for path in glob.glob(pattern, recursive=True):
            if 'node_modules' in path.replace('\\', '/'):
                continue
            try:
                with open(path, 'r') as f:
                    pkg = json.load(f)
            except (json.JSONDecodeError, IOError) as e:
                logging.warning(f"Could not read {path}: {e}")
                continue
            for section in ('dependencies', 'devDependencies'):
                for name, ver in pkg.get(section, {}).items():
                    if ver == '*':
                        continue
                    if any(name.startswith(s) for s in _INTERNAL_SCOPES):
                        continue
                    if name not in versions:
                        versions[name] = ver
        logging.info(f"Collected {len(versions)} package versions from core")
        return versions

class Project:
    def __init__(self, dir, buildDir, buildMode, versions=None, resolutionVersions=None):
        webUiDir=os.path.join(dir, "web-ui")
        buildWebUiDir=os.path.join(buildDir, "web-ui")
        # Sync the source package.json with current core versions so that
        # committed files have real version constraints (not "*"), which
        # prevents GitHub Dependabot warnings and makes the version history
        # track esp32m upgrades automatically.
        if versions and buildDir != dir:
            sourcePackageJson = PackageJson(os.path.join(webUiDir, "package.json"))
            if sourcePackageJson.exists():
                if sourcePackageJson.syncVersions(versions):
                    logging.info(f"Synced package versions in {sourcePackageJson.path}")
                    sourcePackageJson.dump(indent=2)
        package=Package(buildWebUiDir)
        if buildDir!=dir:
            if not package.packageJson.exists() or buildMode==BuildMode.Full:
                copyPackage(webUiDir, buildWebUiDir)
        package.packageJson.removeResolutions(resolutionVersions or versions)
        package.packageJson.removeTypeModule()
        package.packageJson.dump()
        self.package=package
        self.buildMode=buildMode

    def generateResources(self):
        dir=self.package.distDir
        if not os.path.isdir(dir):
            os.makedirs(dir)
        types = [{'ext': 'html', 'ct': 'text/html; charset=UTF-8'},
                {'ext': 'js', "ct": 'application/javascript'}]
        assets = []
        for t in types:
            for sfp in glob.glob(os.path.join(dir, "*."+t['ext'])):
                cfp = sfp+'.gz'
                with open(sfp, 'rb') as pf, open(cfp, 'wb') as cf, gzip.GzipFile('', mode='wb', fileobj=cf) as zf:
                    shutil.copyfileobj(pf, zf)
                afp = sfp+'.S'
                name = os.path.basename(sfp)
                bin2asm(cfp, afp, sanitizeName(name))
                with open(cfp, 'rb') as cf:
                    buf = cf.read()
                assets.append(
                    {'name': name, 'ct': t['ct'], 'ce': 'gzip', 'size': len(buf), 'hash': hashlib.sha1(buf).digest()})
        for name in ['main.js.S', 'index.html.S']:
            p=os.path.join(dir, name)
            n=sanitizeName(name)
            if not os.path.exists(p):
                with open(p, 'w', newline='\n') as df:
                    df.write(f'.data\n.section .rodata.embedded\n.global {n}_start\n{n}_start:\n.global {name}_end\n{name}_end:\n')
        if len(assets) != 0 or self.buildMode!=BuildMode.Full:
            with open(os.path.join(dir, "ui.hpp"), 'w', newline='\n') as f:
                f.write("// This is auto-generated file, do not edit!\n")
                f.write("// This file is to be included only once from main.cpp!\n\n")
                f.write("#pragma once\n\n")
                f.write("namespace esp32m {\n\n")
                for a in assets:
                    name = sanitizeName(a['name'])
                    f.write(f'  extern "C" const uint8_t {name}_start[];\n')
                    f.write(f'  extern "C" const uint8_t {name}_end[];\n')
                f.write("\n  static inline void initUi(Ui* ui) {\n")
                for a in assets:
                    name = sanitizeName(a['name'])
                    url = "/"
                    if not a['name'].startswith('index.htm'):
                        url += a['name']
                    etag = f"{a['size']}-{base64.b64encode(a['hash']).decode('ascii')[:27]}"
                    f.write(
                        f'    ui->addAsset("{url}", "{a["ct"]}", {name}_start, {name}_end, "{a["ce"]}", "\\"{etag}\\"");\n')
                f.write("  }\n")
                f.write("}\n")

    def uiHppExists(self):
        return os.path.exists(os.path.join(self.package.distDir, "ui.hpp"))

def copyPackage(fromDir, toDir, level=0):
    if not os.path.exists(toDir):
        os.makedirs(toDir)
    for item in os.listdir(fromDir):
        s = os.path.join(fromDir, item)
        if os.path.isdir(s): 
            if item == "node_modules":
                continue
            if level==0 and item in ["dist", "tests", "coverage", "build"]:
                continue
        elif os.path.isfile(s):
            if item in ["CMakeLists.txt"]:
                continue
        d = os.path.join(toDir, item)
        if os.path.isdir(s):
            copyPackage(s, d, level+1)
        else:
            shutil.copy(s, d)

    
def sanitizeName(n):
    return re.sub(r'[^a-zA-Z0-9_]', '_', n)

def bin2asm(source, dest, name):
    with open(source, 'rb') as sf, open(dest, 'w', newline='\n') as df:
        df.write(
            f'.data\n.section .rodata.embedded\n.global {name}_start\n{name}_start:\n')
        while True:
            linebuf = sf.read(16)
            if len(linebuf) == 0:
                break
            line = ""
            for x in linebuf:
                if len(line) != 0:
                    line += ","
                line += (" 0x"+bytes([x]).hex())
            df.write(".byte" + line + "\n")
        df.write(f'.global {name}_end\n{name}_end:\n')

def main():
    logging.basicConfig(
        format='esp32m-ui:%(levelname)s:%(message)s', level=logging.INFO)
    parser = argparse.ArgumentParser()
    parser.add_argument("--esp32m-dir", dest="esp32mDir", required=True, help="root of the esp32m manager component")
    parser.add_argument("--source-dir", dest="sourceDir", required=True, help="project root")
    parser.add_argument("--build-dir", dest="buildDir", required=True, help="destination directory for the compiled UI files")
    parser.add_argument("--build-mode", dest="buildMode", required=True, help="UI build mode", type=int)
    args = parser.parse_args()
    esp32m=Esp32m(args.esp32mDir)
    buildMode=BuildMode(args.buildMode)
    versions=esp32m.collectVersions()
    resolutionVersions=_parse_yarn_lock(os.path.join(esp32m.package.dir, "yarn.lock"))
    project=Project(args.sourceDir, args.buildDir, buildMode, versions, resolutionVersions)
    buildUi=False
    if buildMode==BuildMode.Full:
        buildUi=True
        logging.info ("building UI because UI build mode is set to FULL")
    elif buildMode==BuildMode.Once:
        if project.uiHppExists():
            logging.info ("not building UI because dist/ui.hpp exists and UI build mode is set to ONCE")
        else:
            buildUi=True
            logging.info ("building UI because dist/ui.hpp does not exist and UI build mode is set to ONCE")
    elif buildMode==BuildMode.Never:
            logging.info ("not building UI because UI build mode is set to NEVER")
    else:
        raise Exception(f"unknown UI build mode: {buildMode}") 
    if buildUi:
        yarn=Yarn(project.package.dir)
        yarn.link(esp32m.package.dir)
        yarn.build()
    project.generateResources()


if __name__ == "__main__":
    main()
