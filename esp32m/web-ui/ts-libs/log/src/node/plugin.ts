import { TYargsPlugin } from "@ts-libs/config";
import { LevelNames } from "../types";
import { setLogLevel } from "../impl";

export const Name = 'log';

export const pluginLog: TYargsPlugin = {
    name: Name,
    yargs: {
        init: (argv) => {
            argv.option('log-level', { choices: LevelNames.filter(l => !!l), description: 'Log level' });
        },
        parsed: async (argv) => {
            const level = (await Promise.resolve(argv))['log-level'];
            if (level !== undefined) {
                setLogLevel(level as string);
            }
        }
    }
};
