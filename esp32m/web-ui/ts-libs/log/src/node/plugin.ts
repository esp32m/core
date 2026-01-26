import { TYargsPlugin } from "@ts-libs/config";
import { LevelNames } from "../types";

export const Name = 'log';

export const pluginLog: TYargsPlugin = {
    name: Name,
    yargs: {
        init: (argv) => {
            argv.option('log-level', { choices: LevelNames.filter(l => !!l), description: 'Log level' });
        },
    }
};
