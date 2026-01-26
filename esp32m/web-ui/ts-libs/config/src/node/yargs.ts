import { getPlugins, TPlugin } from '@ts-libs/plugins';
import yargs from 'yargs';
import { hideBin } from 'yargs/helpers';

let argv: ReturnType<typeof yargs.parse>;
let resolveArgv: ((value: ReturnType<typeof yargs.parse>) => void) | undefined;

const argvPromise = new Promise<ReturnType<typeof yargs.parse>>((resolve) => {
    resolveArgv = resolve;
});

type TYargsInit = (instance: ReturnType<typeof yargs>) => void;

export type TYargsPlugin = TPlugin & {
    readonly yargs: { readonly init: TYargsInit };
};


export const initYargs = () => {
    if (argv)
        throw new Error('BUG: Yargs already initialized');
    const y = yargs(hideBin(process.argv));
    const plugins = getPlugins<TYargsPlugin>((p) => !!p.yargs?.init);
    for (const p of plugins) try {
        p.yargs.init(y);
    } catch (e) {
        console.warn(`Yargs plugin ${p.name} init error:`, e);
    }
    argv = y.help().alias('help', 'h').parse();
    if (resolveArgv) {
        resolveArgv(argv);
        resolveArgv = undefined;
    } else throw new Error('BUG: resolveArgv is undefined');
    return argv;
}

export const getArgv = () => {
    return argvPromise;
};