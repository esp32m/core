import { createAction } from '@reduxjs/toolkit';
import { DiscreteStatus, TDisposer, TJsonValue } from '@ts-libs/tools';
import { Observable } from 'rxjs';
import { TBackendStateRoot } from './state';

export const enum ConnectionStatus {
  Disconnected = 'disconnected',
  Connecting = 'connecting',
  Connected = 'connected',
}

export enum MessageType {
  Request = 'request',
  Response = 'response',
  Broadcast = 'broadcast',
}

export enum MessageName {
  GetState = 'state-get',
  SetState = 'state-set',
  GetConfig = 'config-get',
  SetConfig = 'config-set',
  GetInfo = 'info-get',
}


export type TMessage = {
  type: MessageType;
  seq: number;
  data?: any;
};

export type TRequest = TMessage & {
  type: MessageType.Request;
  name: string;
  target?: string;
};

export type TResponse = TMessage & {
  type: MessageType.Response;
  name: string;
  source: string;
  partial: boolean;
  error?: any;
};

export type TBroadcast = TMessage & {
  type: MessageType.Broadcast;
  name: string;
  source: string;
};

export interface IModuleApi {
  readonly api: IBackendApi;
  readonly name: string;
  readonly selectors: {
    readonly state: (state: TBackendStateRoot) => TJsonValue | undefined;
    readonly config: (state: TBackendStateRoot) => TJsonValue | undefined;
    readonly info: (state: TBackendStateRoot) => TJsonValue | undefined;
    readonly activeRequests: (state: TBackendStateRoot) => Record<string, number>;
    readonly isSettingState: (state: TBackendStateRoot) => boolean | undefined;
    readonly isSettingConfig: (state: TBackendStateRoot) => boolean | undefined;
  };
  useState(data?: any): VoidFunction;
}

export interface IStatePoller {
  suspend(): TDisposer;
}

export type TRequestOptions = {
  timeout?: number;
}

export interface IBackendApi {
  readonly status: DiscreteStatus<ConnectionStatus>;
  readonly incoming: Observable<TMessage>;
  readonly outgoing: Observable<TRequest>;
  readonly resolved: Observable<[TRequest, TResponse]>;
  readonly rejected: Observable<[TRequest, Error]>;
  readonly statePoller: IStatePoller;
  open(): Promise<void>;
  close(): void;
  module(name: string): IModuleApi;
  request(target: string, name: string, data?: any, options?: TRequestOptions): Promise<TResponse>;
  getInfo(target: string, data?: any): Promise<TResponse>;
  getState(target: string, data?: any): Promise<TResponse>;
  setState(target: string, data?: any): Promise<TResponse>;
  getConfig(target: string, data?: any): Promise<TResponse>;
  setConfig(target: string, data?: any): Promise<TResponse>;
}

export const actions = {
  request: createAction<TRequest>(`esp32m/${MessageType.Request}`),
  requestResolved: createAction<[TRequest, TResponse]>(`esp32m/${MessageType.Request}-resolved`),
  requestRejected: createAction<[TRequest, TJsonValue]>(`esp32m/${MessageType.Request}-rejected`),
  response: createAction<TResponse>(`esp32m/${MessageType.Response}`),
  broadcast: createAction<TBroadcast>(`esp32m/${MessageType.Broadcast}`),
};
