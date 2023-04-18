import os
import platform
import re
import sys
import glob
import collections.abc
import gzip
import shutil
import hashlib
import base64
import subprocess
import json
import logging
import importlib
import importlib.util
import contextlib
import io
import argparse
import string
import zipfile

config = {
    "path": {
        "git": None,
        "clang-format": None,
        "idf": None,
        "idf-tools": None,
        "firmware-target": None,
    }
}


class Idf:
    def __init__(self, dir):
        self.dir = dir
        self.valid = False
        td = os.path.join(dir, "tools")
        tp = os.path.join(td, "idf_tools.py")
        if not os.path.exists(tp):
            return
        sys.path.insert(0, td)
        spec = importlib.util.spec_from_file_location("idf_tools", tp)
        idftools = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(idftools)
        f = io.StringIO()
        with contextlib.redirect_stdout(f):
            idftools.main(["export", "--format", "key-value"])
        f.seek(0)
        while True:
            l = f.readline()
            if not l:
                break
            s = l.rstrip().split('=', 1)
            if len(s) == 2:
                k = s[0]
                v = os.path.expandvars(s[1])
                os.environ[k] = v
                if k == "IDF_PYTHON_ENV_PATH":
                    self.pythonEnvPath = v
        self.idf = os.path.realpath(shutil.which("idf.py"))
        if not os.path.exists(self.idf):
            return
        self.idf_version = subprocess.check_output([self.idf, "--version"])
        self.valid = self.idf_version is not None
        if self.valid:
            self.idf_version = self.idf_version.decode('ascii').rstrip()
            logging.info(f"using ESP-IDF v.{self.idf_version} at {self.idf}")

    def run(self, args):
        try:
            r = subprocess.run([self.idf]+args)
            return r.returncode == 0
        except Exception as e:
            logging.error(e)
            return False


class Cml:
    def __init__(self, dir):
        self.path = os.path.join(dir, "CMakeLists.txt")
        self.valid = False
        if not os.path.exists(self.path):
            logging.warning(f"file CMakeLists.txt not found in {dir}")
            return
        with open(self.path, 'r') as f:
            while True:
                line = f.readline()
                if not line:
                    break
                m = re.search('^project\(([^ ]+)(?: VERSION ([^ ]+))?\)$', line)
                if m:
                    self.name = m.group(1)
                    version = m.group(2)
                    if version:
                        self.version = version

        self.valid = True


class Project:
    def __init__(self, args):
        self.args = args
        self.dir = os.path.realpath(args.projdir)
        self.valid = False
        self.idf = bootstrap()
        self.cmls = []
        self.ui = 'build'
        if args.ui and args.ui[0]:
            self.ui = args.ui[0]
        self.useUi = self.ui != 'skip'
        self.init = len(args.command) == 1 and args.command[0] == "init"
        self.build = "build" in args.command
        if self.init:
            if os.listdir(args.projdir):
                logging.error(f"directory {self.dir} is not empty")
                exit(1)
            scriptdir = os.path.dirname(os.path.realpath(__file__))
            ecds = '"'+os.path.join(scriptdir,
                                    "esp32m").replace(os.sep, '/')+'"'
            map = {}
            if self.useUi:
                ecds += ' "web-ui"'
                map["PRIV_REQUIRES"] = "web-ui"
            map["PROJECT_NAME"] = os.path.basename(os.path.normpath(self.dir))
            map["EXTRA_COMPONENT_DIRS"] = ecds
            copyTemplate(os.path.join(scriptdir, "template"),
                         self.dir, map, self.useUi)
        for f in [".", "main"]:
            cml = Cml(os.path.join(self.dir, f))
            if not cml.valid:
                return
            self.cmls.append(cml)
        p = os.path.join(self.dir, "web-ui/package.json")
        if os.path.exists(p):
            yarn = shutil.which("yarn")
            if not yarn:
                logging.warning(
                    "please install yarn to build project with web UI")
                return
            self.yarn = os.path.realpath(yarn)
            self.uidir = os.path.join(self.dir, "web-ui")
        self.valid = True

    def run(self):
        if self.init:
            return
        if self.build and self.useUi and self.yarn:
            if not os.path.exists(os.path.join(self.uidir, "yarn.lock")) or not os.path.exists(os.path.join(self.uidir, "node_modules")):
                subprocess.run([self.yarn, 'install'], cwd=self.uidir)
            subprocess.run([self.yarn, self.ui], cwd=self.uidir)
            prepareUi(os.path.join(self.uidir, "dist"))
        cmds = self.args.command
        if self.build:
            bumpVersion(self.dir)
        if self.args.port:
            cmds += ['-p', self.args.port]
        if not self.idf.run(cmds):
            return
        dst = config['path']['firmware-target']
        name = self.cmls[0].name
        if self.build and dst and os.path.isdir(dst) and name:
            src = os.path.join(self.dir, "build", name+".bin")
            if os.path.isfile(src):
                logging.info(f'copying {src} -> {dst}')
                dp = os.path.join(dst, name+".bin")
                shutil.copyfile(src, dp)
                logging.info(f'firmware is ready at {dp}')
            if self.args.zip:
                zipPath = os.path.join(self.dir, "build", name+".zip")
                # esptoolDir = os.path.join(self.idf.pythonEnvPath, "Scripts")
                esptoolDir = os.path.join(self.idf.dir, "components", "esptool_py", "esptool")
                esptoolPath = os.path.join(esptoolDir, "esptool.py")
                builtFiles = [
                    name+".bin",
                    "flash_args",
                    "ota_data_initial.bin",
                    os.path.join("bootloader", "bootloader.bin"),
                    os.path.join("partition_table", "partition-table.bin")
                ]
                with zipfile.ZipFile(zipPath, 'w') as zipf:
                    for fn in builtFiles:
                        fp = os.path.join(self.dir, "build", fn)
                        if os.path.exists(fp):
                            zipf.write(fp, fn)
                    if os.path.exists(esptoolPath):
                        zipf.write(esptoolPath, "esptool.py")
                    cmd = "python esptool.py --baud 115200 --chip esp32 write_flash @flash_args"
                    zipf.writestr("flash.cmd", cmd)
                    zipf.writestr("flash.sh", "#!/bin/sh\n"+cmd)
                logging.info(f'compiled binaries are zipped in {zipPath}')


def enumPy(dir, base, list):
    for item in os.listdir(dir):
        p = os.path.join(dir, item)
        bp = os.path.join(base, item)
        if os.path.isdir(p):
            if item != '__pycache__':
                enumPy(p, bp, list)
        else:
            name, ext = os.path.splitext(p)
            if ext in ['.py']:
                list.append(bp)


def which(name):
    p = config["path"][name]
    if p is None:
        p = shutil.which(name)
    return p


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


def sanitizeName(n):
    return re.sub(r'[^a-zA-Z0-9_]', '_', n)


def prepareUi(dir):
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
    if len(assets) != 0:
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


def bumpVersion(dir):
    fn = os.path.join(dir, 'version.txt')
    if not os.path.exists(fn):
        return
    major = 0
    minor = 1
    build = 1
    revision = 0
    try:
        with open(fn, 'r') as f:
            line = f.readline()
        m = re.search('^(\d+)\.(\d+)(?:\.(\d+))?(?:\.(\d+))?$', line)
        if m:
            major = int(m.group(1))
            minor = int(m.group(2))
            if m.group(3):
                build = int(m.group(3))+1
            if m.group(4):
                revision = int(m.group(4))
    except Exception as e:
        logging.error(e)
    version = f'{major}.{minor}.{build}'
    if revision:
        version += "."+str(revision)
    with open(fn, 'w', newline='\n') as f:
        f.write(f'{version}\n')


def deep_update(source, overrides):
    for key, value in overrides.items():
        if isinstance(value, collections.abc.Mapping) and value:
            returned = deep_update(source.get(key, {}), value)
            source[key] = returned
        else:
            source[key] = overrides[key]
    return source


def configure():
    paths = [os.path.dirname(sys.argv[0]), '.']
    for p in paths:
        fp = os.path.join(p, "esp32m.json")
        if (os.path.exists(fp)):
            logging.info("loading config from "+fp)
            try:
                with open(fp, 'r') as f:
                    c = json.load(f)
                deep_update(config, c)
            except Exception as e:
                logging.error(e)


def bootstrap():
    git = which("git")
    if not git:
        raise "git not found, please install or provide path to git executable in config"
    idfpaths = []
    cp = config["path"]["idf"]
    if cp:
        idfpaths.append(cp)
    dir = os.path.dirname(sys.argv[0])
    while True:
        p = os.path.abspath(os.path.join(dir, os.pardir))
        if p == dir:
            break
        dir = p
        idfpaths.append(p)

    for p in idfpaths:
        idf = Idf(os.path.join(p, "esp-idf"))
        if idf.valid:
            return idf
    logging.warning("ESP-IDF not found")
    exit(1)


def copyTemplateFile(src, dst, mapping):
    try:
        with open(src, 'r') as fs, open(dst, 'w') as fd:
            lines = fs.readlines()
            for l in lines:
                t = string.Template(l)
                fd.write(t.substitute(mapping))
    except Exception as e:
        logging.error("could not copy file "+src, e)


def copyTemplate(src, dst, mapping, useUi):
    if not os.path.exists(dst):
        os.makedirs(dst)
    for item in os.listdir(src):
        if item == "web-ui" and not useUi:
            continue
        s = os.path.join(src, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            copyTemplate(s, d, mapping, useUi)
        else:
            copyTemplateFile(s, d, mapping)


def formatDir(clf, dir):
    for item in os.listdir(dir):
        if item in ['deps', 'build', 'web-ui']:
            continue
        p = os.path.join(dir, item)
        if os.path.isdir(p):
            formatDir(clf, p)
        else:
            name, ext = os.path.splitext(p)
            if ext in ['.cpp', '.hpp']:
                logging.info("formatting "+p)
                subprocess.run([clf, "--verbose", "-i", "--style=file", p])


def format():
    clf = which("clang-format")
    if not clf:
        raise "clang-format not found, please install or provide path to the executable in config"
    formatDir(clf, ".")
    exit()


if __name__ == "__main__":
    logging.basicConfig(
        format='esp32m:%(levelname)s:%(message)s', level=logging.INFO)
    parser = argparse.ArgumentParser()
    parser.add_argument('--path', nargs=2, action="append",
                        metavar=("component", "path"), help="specify path to dependencies")
    parser.add_argument('--project', dest='projdir', default='.')
    parser.add_argument('--ui', dest='ui', nargs=1, choices=['skip'])
    parser.add_argument('--port', '-p', dest='port', type=str)
    parser.add_argument('--zip', action="store_true",
                        help="create zip file with compiled binaries")
    parser.add_argument('command', nargs='+', help="idf.py command")
    args = parser.parse_args()
    configure()
    if args.path:
        for p in args.path:
            config["path"][p[0]] = p[1]
    if "format" in args.command:
        format()
    project = Project(args)
    if project.valid:
        project.run()
    else:
        exit(1)
