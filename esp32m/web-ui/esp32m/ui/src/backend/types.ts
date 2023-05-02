import { TStateRoot } from '@ts-libs/redux';
import { DiscreteStatus } from '@ts-libs/tools';
import { Observable } from 'rxjs';

export const enum ConnectionStatus {
  Disconnected = 'disconnected',
  Connecting = 'connecting',
  Connected = 'connected',
}

export type TMessage = {
  type: string;
  seq: number;
  data?: any;
};

export type TRequest = TMessage & {
  name: string;
  target?: string;
};

export type TResponse = TMessage & {
  name: string;
  source: string;
  partial: boolean;
  error?: any;
};

export interface IDeviceApi {
  readonly api: IBackendApi;
  readonly name: string;
  readonly selectors: {
    readonly state: (state: TStateRoot) => any;
  };
  useState(): VoidFunction;
}

export interface IBackendApi {
  readonly status: DiscreteStatus<ConnectionStatus>;
  readonly incoming: Observable<TMessage>;
  open(): Promise<void>;
  close(): void;
  device(name: string): IDeviceApi;
  request(target: string, name: string, data?: any): Promise<TResponse>;
  getState(target: string, data?: any): Promise<TResponse>;
  setState(target: string, data?: any): Promise<TResponse>;
  getConfig(target: string, data?: any): Promise<TResponse>;
  setConfig(target: string, data?: any): Promise<TResponse>;
}
