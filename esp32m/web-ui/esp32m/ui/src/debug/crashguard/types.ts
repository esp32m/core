import { ResetReason } from "../../system/types";

export const Name = 'crash-guard';

export enum OtaState {
    New = 0,
    PendingVerify = 1,
    Valid = 2,
    Invalid = 3,
    Aborted = 4,
    Undefined = -1
}

export enum OtaRole {
    Preferred = 0,  // OK and considered the most recent / intended image
    Fallback = 1,   // OK but considered older / not intended
    Failed = 2,     // Rollback marked INVALID/ABORTED (if available)
}

export type TOtaState = {
    strike: number;
    crashes: number;
    brownouts: number;
    early: number;
    good: number;
    sig: string;
}

export enum Checkpoint {
    Boot = 0,
    Init0 = 10,
    Inited = 20,
    OtaReady = 30,
    Stable = 40,
    Done = 250,
};


export type TRunState = {
    valid?: boolean;
    boot: number;
    slot: number;
    rr?: ResetReason;
    cp: number;
    inProgress: boolean;
    planned: boolean;
    unexpected?: boolean;
    uptime: number;
    sig: string;
    ts?: number;
    coredump?: {
        uid: number;
    }
}

export type TState = {
    uptime: number;
    slot: number;
    ota: Array<TOtaState>;
    rtc: TRunState;
}

export type TOtaInfo = {
    label: string;
    app: {
        proj: string;
        ver: string;
        date: string;
        time: string;
        sha256: string;
    }
    otaState: OtaState;
    role: OtaRole
}

export type TInfo = {
    boot: number;
    lastSwitch: number
    ota: Array<TOtaInfo>;
    coredump: {
        uid: string;
        slot: number;
    }
    last: TRunState;
    history: Array<TRunState>;
}
