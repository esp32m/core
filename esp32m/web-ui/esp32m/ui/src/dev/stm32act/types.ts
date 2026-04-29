export const Name = 'stm32act';

export type TMotorInfo = {
    name: string;
    title: string;
    pwm: boolean;
    disabled: boolean;
}

export type TInfo = {
    appver: number[];
    uid: number[];
    clk: { sys: number; h: number; p1: number; p2: number };
    rf: number;
    wdtc: number;
    cc: number;
    motors: TMotorInfo[];
}

export type TBootloaderInfo = {
    app?: {
        maxSize?: number;
        tailSize?: number;
        tailMagic?: number;
        maxWriteSize?: number;
    }
}

export enum MotorState {
    Off,
    Forward,
    Reverse,
    Brake,
    Shutdown,
}

export enum BootMode {
    Normal,
    Bootloader,
}

export enum MotionEventKind {
    None,
    TerminalCandidate,
    TerminalConfirmed,
    Anomaly,
}

export enum OpenState {
    Unknown,
    Opening,
    Open,
    Closing,
    Closed,
};

export type TMotorState = [
    state: MotorState,
    speed: number,
    current: number,
    fastCurrent: number,
    eventKind: MotionEventKind,
    eventDirection: MotorState,
    elapsedMs: number,
    peakCurrent: number,
    confidence: number,
    eventSeq: number,
    open: OpenState,
    position: number,
];

export type TState = {
    mode: 'normal' | 'bootloader';
    uptime: number;
    heap: { total: number; free: number; minFree: number };
    cpuUsage: number;
    threadCount: number;
    vrefint: number;
    mcutemp: number;
    ntc: number;
    boot: BootMode;
    current: number;
    motors: TMotorState[];
}
export type TWindowConfig = {
    terminalOpen: boolean;
    terminalClose: boolean;
    openTimeout: number;
    closeTimeout: number;
}

export type TConfig = {
    windows: TWindowConfig[];
}