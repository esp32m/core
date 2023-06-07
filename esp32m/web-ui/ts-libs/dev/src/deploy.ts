import Sftp from 'ssh2-sftp-client';
import Ssh2 from 'ssh2-promise';
import fs from 'fs';
import path from 'path';
import rl from 'readline';
import { exec } from 'child_process';

type TDeployConfig = {
  projdir: string;
  uidir?: string;
  host: string;
  port?: number;
  username: string;
  privateKey?: string | Buffer;
  passphrase?: string;
};

class Project {
  readonly packageJson: any;
  readonly safeName: string;
  constructor(readonly dir: string) {
    this.packageJson = JSON.parse(
      fs.readFileSync(path.join(dir, 'package.json')).toString()
    );
    this.safeName = this.packageJson.name.replace('@', '').replace('/', '-');
  }
  yarn(script: string) {
    return new Promise((resolve, reject) => {
      console.log(`running yarn ${script} in ${this.dir}`);
      exec('yarn ' + script, { cwd: this.dir }, (error, stdout, stderr) => {
        if (error) reject(error);
        else {
          if (stdout) console.log(stdout);
          if (stderr) console.warn(stderr);
          resolve(stdout);
        }
      });
    });
  }
}

export class Deploy {
  readonly project: Project;
  readonly ui?: Project;
  constructor(readonly config: TDeployConfig) {
    const { projdir, uidir } = config;
    this.project = new Project(projdir);
    if (uidir) this.ui = new Project(uidir);
  }
  async deploy() {
    try {
      await this.project.yarn('build');
      await this.copyFiles(this.project);
      if (this.ui) {
        await this.ui.yarn('build');
        await this.copyFiles(this.ui, 'ui');
      }
    } finally {
      await this.cleanup();
    }
  }
  async copyFiles(project: Project, subdir?: string) {
    const sftp = await this.getSftp();
    let remotedir = path.posix.join(
      '/home',
      this.config.username,
      this.project.safeName
    );
    if (subdir) remotedir = path.posix.join(remotedir, subdir);
    await sftp.mkdir(remotedir, true);
    for (const filename of ['main.js', 'main.js.map', 'index.html']) {
      const filepath = path.join(project.dir, 'dist', filename);
      if (fs.existsSync(filepath)) {
        const dest = path.posix.join(remotedir, filename);
        console.log(`copying ${filepath} -> ${dest}`);
        await sftp.put(filepath, dest);
      }
    }
  }
  async getSshConfig() {
    if (!this._sshConfig) {
      const {
        projdir: _,
        uidir: __,
        host,
        port = 22,
        username,
        ...rest
      } = this.config;
      let { privateKey, passphrase } = this.config;
      if (privateKey && fs.existsSync(privateKey))
        privateKey = fs.readFileSync(privateKey);
      if (passphrase === 'ask')
        passphrase = await new Promise((resolve) =>
          this.getReadline().question('Enter passphrase: ', resolve)
        );
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
      this._sftp = new Sftp();
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
      this._ssh = new Ssh2(config);
      await this._ssh.connect();
      console.log(`connected to ${config.host}:${config.port} via SSH`);
    }
    return this._sftp;
  }
  getReadline() {
    if (!this._readline)
      this._readline = rl.createInterface({
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
  private _sftp?: Sftp;
  private _ssh?: Ssh2;
  private _sshConfig?: Sftp.ConnectOptions;
  private _readline?: rl.Interface;
}
