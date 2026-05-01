import { getPlugins, TPlugin } from '@ts-libs/plugins';
import yargs, { type Arguments } from 'yargs';
import { hideBin } from 'yargs/helpers';

let argv: Arguments;
let resolveArgv: ((value: Arguments) => void) | undefined;

const argvPromise = new Promise<Arguments>((resolve) => {
    resolveArgv = resolve;
});

type TYargsInit = (instance: ReturnType<typeof yargs>) => void;
type TYargsParsed = (instance: Arguments) => void;

export type TYargsPlugin = TPlugin & {
    readonly yargs: { readonly init?: TYargsInit, readonly parsed?: TYargsParsed  };
};


export const initYargs = async () => {
    if (argv)
        throw new Error('BUG: Yargs already initialized');
    const y = yargs(hideBin(process.argv));
    const plugins = getPlugins<TYargsPlugin>((p) => !!p.yargs?.init || !!p.yargs?.parsed);
    for (const p of plugins) try {
        p.yargs.init?.(y);
    } catch (e) {
        console.warn(`Yargs plugin ${p.name} init error:`, e);
    }
    argv = await y.help().alias('help', 'h').parseAsync();
    for (const p of plugins) try {
        p.yargs.parsed?.(argv);
    } catch (e) {
        console.warn(`Yargs plugin ${p.name} parsed error:`, e);
    }
    if (resolveArgv) {
        resolveArgv(argv);
        resolveArgv = undefined;
    } else throw new Error('BUG: resolveArgv is undefined');
    return argv;
}

export const getArgv = () => {
    return argvPromise;
};