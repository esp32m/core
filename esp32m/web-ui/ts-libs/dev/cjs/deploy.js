"use strict";
var __importDefault = (this && this.__importDefault) || function (mod) {
    return (mod && mod.__esModule) ? mod : { "default": mod };
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.Deploy = void 0;
const ssh2_sftp_client_1 = __importDefault(require("ssh2-sftp-client"));
const ssh2_promise_1 = __importDefault(require("ssh2-promise"));
const fs_1 = __importDefault(require("fs"));
const path_1 = __importDefault(require("path"));
const readline_1 = __importDefault(require("readline"));
const child_process_1 = require("child_process");
class Project {
    config;
    packageJson;
    safeName;
    constructor(config) {
        this.config = config;
        this.packageJson = JSON.parse(fs_1.default.readFileSync(path_1.default.join(config.dir, 'package.json')).toString());
        this.safeName = this.packageJson.name.replace('@', '').replace('/', '-');
    }
    yarn(script) {
        return new Promise((resolve, reject) => {
            console.log(`running yarn ${script} in ${this.config.dir}`);
            (0, child_process_1.exec)('yarn ' + script, { cwd: this.config.dir }, (error, stdout, stderr) => {
                if (error)
                    reject(error);
                else {
                    if (stdout)
                        console.log(stdout);
                    if (stderr)
                        console.warn(stderr);
                    resolve(stdout);
                }
            });
        });
    }
}
class Deploy {
    config;
    projects;
    constructor(config) {
        this.config = config;
        const { projects } = config;
        this.projects = projects.map((c) => new Project(c));
    }
    async deploy() {
        try {
            for (const p of this.projects) {
                await p.yarn(p.config.script || 'build');
                await this.copyFiles(p);
            }
        }
        finally {
            await this.cleanup();
        }
    }
    async copyFiles(project) {
        const sftp = await this.getSftp();
        let remotedir = path_1.default.posix.join('/home', this.config.username, this.projects[0].safeName);
        if (project.config.dest)
            remotedir = path_1.default.posix.join(remotedir, project.config.dest);
        await sftp.mkdir(remotedir, true);
        for (const filename of ['main.js', 'index.html']) {
            const filepath = path_1.default.join(project.config.dir, 'dist', filename);
            if (fs_1.default.existsSync(filepath)) {
                const dest = path_1.default.posix.join(remotedir, filename);
                console.log(`copying ${filepath} -> ${dest}`);
                await sftp.put(filepath, dest);
            }
        }
        const nativeDir = path_1.default.join(project.config.dir, 'dist', 'native');
        if (fs_1.default.existsSync(nativeDir)) {
            const dest = path_1.default.posix.join(remotedir, 'native');
            console.log(`copying directory ${nativeDir} -> ${dest}`);
            sftp.uploadDir(nativeDir, dest);
        }
    }
    async getSshConfig() {
        if (!this._sshConfig) {
            const { projects: _, host, port = 22, username, ...rest } = this.config;
            let { privateKey, passphrase } = this.config;
            if (privateKey && fs_1.default.existsSync(privateKey))
                privateKey = fs_1.default.readFileSync(privateKey);
            if (passphrase === 'ask')
                passphrase = await new Promise((resolve) => this.getReadline().question('Enter passphrase: ', resolve));
            this._sshConfig = {
                ...rest,
                host,
                port,
                username,
                privateKey,
                passphrase,
            };
        }
        return this._sshConfig;
    }
    async getSftp() {
        if (!this._sftp) {
            const config = await this.getSshConfig();
            this._sftp = new ssh2_sftp_client_1.default();
            await this._sftp.connect(config);
            console.log(`connected to ${config.host}:${config.port} via SFTP`);
            this._sftp.on('upload', (info) => {
                console.log(`uploaded ${info.source}`);
            });
        }
        return this._sftp;
    }
    async getSsh() {
        if (!this._ssh) {
            const config = await this.getSshConfig();
            this._ssh = new ssh2_promise_1.default(config);
            await this._ssh.connect();
            console.log(`connected to ${config.host}:${config.port} via SSH`);
        }
        return this._sftp;
    }
    getReadline() {
        if (!this._readline)
            this._readline = readline_1.default.createInterface({
                input: process.stdin,
                output: process.stdout,
            });
        return this._readline;
    }
    async cleanup() {
        const sftp = this._sftp;
        if (sftp) {
            delete this._sftp;
            await sftp.end();
            console.log('SFTP connection closed');
        }
        const ssh = this._ssh;
        if (ssh) {
            delete this._ssh;
            await ssh.close();
            console.log('SSH connection closed');
        }
        const readline = this._readline;
        if (readline) {
            delete this._readline;
            readline.close();
        }
    }
    _sftp;
    _ssh;
    _sshConfig;
    _readline;
}
exports.Deploy = Deploy;
//# sourceMappingURL=deploy.js.map