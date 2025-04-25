export const Name = "md-host";

export enum BliState {
    Preparing = 0,
    Requesting = 1,
    PowerOff = 2,
    PowerOn = 3,
    Ping = 4,
    Ready = 5,
}

export type TLocalState = {
    bliState: number;
    error?: string;
    stateChanging: Record<string, boolean>;
}


export type TState = { mds: Array<string>, bls: Record<string, number> };

export type TMotorState = {
    op: number;
    speed: number;
    current: number;
}

export type TDevState = {
    temperature?: number;
    uptime?: number;
    sys?: {
        cpu?: number;
        fh?: number;
        mh?: number;
        ms?: number;
        st?: number;
        tc?: number;
    };
    led?: {
        r?: number;
        g?: number;
        b?: number;
    };
    m1?: TMotorState;
    m2?: TMotorState;
};

export type TDevInfo = {
    serial?: number[];
    appver?: number[];
    clk?: {
        sys?: number;
        h?: number;
        p1?: number;
        p2?: number;
    }
    rr?: number;
    ccrash?: number;
    cwdt?: number;
};

export type TInfo = {
    app?: {
        maxSize?: number;
        tailSize?: number;
        tailMagic?: number;
    }
}
