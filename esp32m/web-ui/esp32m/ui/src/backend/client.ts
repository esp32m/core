import {
  DiscreteStatus,
  Periodic,
  debounce,
  isString,
  serializeError,
} from '@ts-libs/tools';
import {
  ConnectionStatus,
  IBackendApi,
  IDeviceApi,
  TMessage,
  TRequest,
  TResponse,
} from './types';
import { Subject } from 'rxjs';
import WebSocket from 'reconnecting-websocket';
import { selectors } from './state';
import { createSelector } from '@reduxjs/toolkit';

interface RequestOptions {
  timeout?: number;
}

type PendingRequestResolver = (response: TResponse) => void;
type PendingRequestRejector = (error: any) => void;
type PendingRequestPartialResponse = (response: TResponse) => void;

type TPendingRequest = {
  request: TRequest;
  resolve: PendingRequestResolver;
  reject: PendingRequestRejector;
  partial: PendingRequestPartialResponse;
  promise: Promise<TResponse>;
  queued: number;
  sent?: number;
};

export function isResponse(msg: TMessage): msg is TResponse {
  return msg.type === 'response' && isString((msg as TResponse).source);
}

class Device implements IDeviceApi {
  readonly selectors;
  constructor(readonly api: Client, readonly name: string) {
    this.selectors = {
      state: createSelector(
        selectors.devices,
        (devices) => devices[name]?.state
      ),
    };
  }
  useState(): VoidFunction {
    this._statePollRefs++;
    return () => {
      if (this._statePollRefs <= 0)
        console.error(
          `esp32m UI bug: ${this.name}.statePollRefs=${this._statePollRefs}`
        );
      else this._statePollRefs--;
    };
  }
  isPolling() {
    return this._statePollRefs > 0;
  }
  private _statePollRefs = 0;
}

class Client implements IBackendApi {
  readonly status = new DiscreteStatus(ConnectionStatus.Disconnected);
  readonly incoming = new Subject<TMessage>();
  constructor(readonly ws: WebSocket) {
    ws.onopen = () => {
      this.status.set(ConnectionStatus.Connected);
      this._statePoller.start();
    };
    ws.onclose = () => {
      this.status.set(ConnectionStatus.Connecting);
      this._statePoller.stop();
    };
    ws.onerror = (e: any) => {
      this.status.set(ConnectionStatus.Connecting, serializeError(e));
    };
    ws.onmessage = (msg: MessageEvent) => {
      const parsed = JSON.parse(msg.data);
      if (parsed) {
        const { type, ...payload } = parsed;
        if (isString(type)) this.incoming.next(parsed);
        if (isResponse(parsed)) {
          const pending = this._pending[payload.seq];
          if (pending)
            if (parsed.partial) pending.partial(parsed);
            else if (parsed.error) pending.reject(parsed.error);
            else pending.resolve(parsed);
        }
      }
    };
  }
  device(name: string): IDeviceApi {
    return (
      this._devices[name] || (this._devices[name] = new Device(this, name))
    );
  }
  async open() {
    this.status.set(ConnectionStatus.Connecting);
    this.ws.reconnect();
  }
  close() {
    this.ws.close();
  }

  request(
    target: string,
    name: string,
    data?: any,
    options?: RequestOptions
  ): Promise<TResponse> {
    const existing = Object.values(this._pending).find(
      (pr) =>
        pr.request.name === name &&
        pr.request.target === target &&
        pr.request.data === data
    );
    if (existing) return existing.promise;
    const seq = ++this._seq;
    const request: TRequest = {
      type: 'request',
      target,
      name,
      seq,
      data,
    };
    let resolve: PendingRequestResolver = () => undefined;
    let reject: PendingRequestRejector = () => undefined;
    let partial: PendingRequestPartialResponse = () => undefined;
    const promise = new Promise<TResponse>((resolveFn, rejectFn) => {
      const capturedSeq = seq;
      const startTimer = () =>
        setTimeout(() => {
          delete this._pending[capturedSeq];
          rejectFn('timeout');
        }, options?.timeout || 10000);
      let timer = startTimer();
      resolve = (response) => {
        clearTimeout(timer);
        delete this._pending[capturedSeq];
        resolveFn(response);
      };
      reject = (error) => {
        clearTimeout(timer);
        delete this._pending[capturedSeq];
        rejectFn(error);
      };
      partial = () => {
        clearTimeout(timer);
        timer = startTimer();
      };
    });
    this._pending[seq] = {
      request,
      resolve,
      reject,
      partial,
      promise,
      queued: Date.now(),
    };
    this.flush();
    return promise;
  }
  getState(target: string, data?: any): Promise<TResponse> {
    return this.request(target, 'state-get', data);
  }
  setState(target: string, data?: any): Promise<TResponse> {
    return this.request(target, 'state-set', data);
  }
  getConfig(target: string, data?: any): Promise<TResponse> {
    return this.request(target, 'config-get', data);
  }
  setConfig(target: string, data?: any): Promise<TResponse> {
    return this.request(target, 'config-set', data);
  }

  private readonly flush = debounce(() => {
    const toSend = Object.values(this._pending).reduce((arr, pr) => {
      if (!pr.sent) {
        pr.sent = Date.now();
        arr.push(pr.request);
      }
      return arr;
    }, [] as Array<TRequest>);
    if (!toSend.length) return;
    this.ws.send(JSON.stringify(toSend));
  }, 100);

  private _seq: number = Math.floor(Math.random() * (Math.pow(2, 31) / 2));
  private readonly _pending: { [key: number]: TPendingRequest } = {};
  private readonly _devices: Record<string, Device> = {};
  private readonly _statePoller = new Periodic(async () => {
    const tasks = Object.values(this._devices)
      .filter((d) => d.isPolling())
      .map((d) => this.getState(d.name));
    await Promise.allSettled(tasks);
  }, 1000);
}

export const newClient = (url: string) => {
  const ws = new WebSocket(url, undefined, { startClosed: true });
  const client = new Client(ws);
  return client;
};

type TEsp32mConfig = {
  backend?: {
    host?: string;
  };
};

declare global {
  interface Window {
    esp32m: TEsp32mConfig;
  }
}

const config = typeof window === 'undefined' ? undefined : window.esp32m;

const defaultUrl =
  'ws://' +
  (config?.backend?.host ||
    (typeof window === 'undefined' ? 'localhost' : window.location.host)) +
  '/ws';

let clientInstance: IBackendApi;

export const client = () =>
  clientInstance || (clientInstance = newClient(defaultUrl));
