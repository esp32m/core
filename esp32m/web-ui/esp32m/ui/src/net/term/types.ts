export const Name = "uart-ws";

export type TState = {
    readonly armed: boolean;
    readonly active: boolean;
    readonly uartOwner: string;
}

export type TConfig = {
    port: number;
    txGpio: number;
    rxGpio: number;
    baudrate: number;
    dataBits: number;
    parity: number;
    stopBits: number;
    flowCtrl: number;
    rxFlowCtrlThresh: number;
    sourceClk: number;
};