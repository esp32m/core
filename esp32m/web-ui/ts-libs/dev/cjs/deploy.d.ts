/// <reference types="node" />
/// <reference types="node" />
import Sftp from 'ssh2-sftp-client';
import rl from 'readline';
type TProjectConfig = {
    dir: string;
    dest?: string;
    script?: string;
};
type TDeployConfig = {
    projects: TProjectConfig[];
    host: string;
    port?: number;
    username: string;
    privateKey?: string | Buffer;
    passphrase?: string;
};
declare class Project {
    readonly config: TProjectConfig;
    readonly packageJson: any;
    readonly safeName: string;
    constructor(config: TProjectConfig);
    yarn(script: string): Promise<unknown>;
}
export declare class Deploy {
    readonly config: TDeployConfig;
    readonly projects: Array<Project>;
    constructor(config: TDeployConfig);
    deploy(): Promise<void>;
    copyFiles(project: Project): Promise<void>;
    getSshConfig(): Promise<Sftp.ConnectOptions>;
    getSftp(): Promise<Sftp>;
    getSsh(): Promise<Sftp | undefined>;
    getReadline(): rl.Interface;
    cleanup(): Promise<void>;
    private _sftp?;
    private _ssh?;
    private _sshConfig?;
    private _readline?;
}
export {};
//# sourceMappingURL=deploy.d.ts.map