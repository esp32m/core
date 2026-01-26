import os from 'os';
import { getArgv, TYargsPlugin } from "./yargs";
import { getPlugins, TPlugin } from '@ts-libs/plugins';

export const Name = 'config-workdir';

class WorkdirBuildContext {
    workdir: string;
    constructor(
        readonly base: string
    ) {
        this.workdir = base;
    }
};

type TWorkdirBulder = (ctx: WorkdirBuildContext) => void

export type TWorkdirBuilderPlugin = TPlugin & {
    readonly workdir: { readonly builder: TWorkdirBulder };
};

export const pluginWorkdir = (): TYargsPlugin => {
    return {
        name: Name,
        yargs: {
            init: (argv) => {
                argv.option('workdir', { alias: 'w', type: 'string', description: 'Working directory' });
            },
        }
    }
};

export const getWorkdir = async (): Promise<string> => {
    const argv = await getArgv();
    let workdir = (argv.workdir as string | undefined) || os.homedir();
    const plugins = getPlugins<TWorkdirBuilderPlugin>((p) => !!p.workdir?.builder);
    if (plugins.length) {
        const ctx = new WorkdirBuildContext(workdir);
        for (const p of plugins) {
            p.workdir.builder(ctx);
        }
        workdir = ctx.workdir;
    }
    return workdir;
}