import { PackageJson } from 'types-package-json';
import { Configuration } from 'webpack';
import 'webpack-dev-server';
declare enum Destination {
    Local = "local",
    Remote = "remote",
    Analyze = "analyze"
}
type BuilderConfig = {
    defines?: Record<string, any>;
};
export declare class WebpackConfigBuilder {
    readonly builderConfig?: BuilderConfig | undefined;
    readonly baseConfig: Configuration;
    readonly config: Configuration;
    readonly dir: string;
    package: PackageJson;
    destination: Destination | undefined;
    constructor(dir: string, config?: Configuration, builderConfig?: BuilderConfig | undefined);
    setTarget(target: Configuration['target']): this;
    isDevelopment(): boolean;
    isProduction(): boolean;
    hasWebTargets(): boolean;
    hasNodeTarget(): boolean;
    private findModule;
    private buildBabelLoader;
    private buildModule;
    private buildOptimization;
    private buildPerformance;
    private buildResolve;
    private buildInfoPlugin;
    private buildPlugins;
    private buildOutput;
    build(): (env: Record<string, string>, argv: Record<string, string>) => any;
}
export declare function runWebpack(dir: string, env?: Record<string, string>, argv?: Record<string, string>): void;
export {};
//# sourceMappingURL=webpack.d.ts.map