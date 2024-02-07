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
    def removeResolutions(self):
        self.ensureLoaded()
        if "resolutions" in self.json:
            del self.json["resolutions"]
        self.json["resolutions"]={
            "react":"*",
            "lodash":"*",
            "webpack":"*",
            "webpack-cli":"*",
            "webpack-dev-server":"*",
        }
    def dump(self):
        if not self.json:
            raise Exception("nothing to dump")
        with open(self.path, 'w') as f:
            json.dump(self.json, f, indent=4)

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

class Project:
    def __init__(self, dir, buildDir, buildMode):
        webUiDir=os.path.join(dir, "web-ui")
        buildWebUiDir=os.path.join(buildDir, "web-ui")
        package=Package(buildWebUiDir)
        if buildDir!=dir:
            if not package.packageJson.exists() or buildMode==BuildMode.Full:
                copyPackage(webUiDir, buildWebUiDir)
        package.packageJson.removeResolutions()
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
    project=Project(args.sourceDir, args.buildDir, buildMode)
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
