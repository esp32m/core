export const Name = 'CSF09';

export type TProps = {
  name?: string;
  title?: string;
  addr?: number;
};

type TReadingState = [
  sampledAtMs: number,
  address: number,
  moisture: number | null,
  temperature: number | null,
];

export type TState =
  | TReadingState
  | [...TReadingState, communicationPending: boolean, lastError: number];

export type TConfig = {
  address: number;
  baud: 9600;
  pendingAddress?: number;
  communicationError?: number;
  lastError?: number;
};
