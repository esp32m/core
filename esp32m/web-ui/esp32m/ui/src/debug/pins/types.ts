export type TPWMState = { enabled: boolean; freq: number; duty: number };
export const enum GpioMode {
  Disabled = 0,
  Input = 1,
  Output = 2,
  InputOutput = 3,
  OutputOd = 2 + 4,
  InputOutputOd = 3 + 4,
}

export const enum PullMode {
  Up = 0,
  Down = 1,
  Both = 2,
  Floating = 3,
}
