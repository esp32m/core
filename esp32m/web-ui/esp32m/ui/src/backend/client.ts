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
  IModuleApi,
  TBroadcast,
  TMessage,
  TRequest,
  TResponse,
  TRequestOptions,
  MessageName,
  MessageType,
  IStatePoller,
} from './types';
import { Subject } from 'rxjs';
import WebSocket from 'reconnecting-websocket';
import { selectors } from './state';
import { createSelector } from '@reduxjs/toolkit';
import { deserializeEsp32mError } from './errors';

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
export function isBroadcast(msg: TMessage): msg is TBroadcast {
  return msg.type === 'broadcast' && isString((msg as TBroadcast).source);
}

class Module implements IModuleApi {
  readonly selectors;
  constructor(
    readonly api: Client,
    readonly name: string
  ) {
    const
      activeRequests = createSelector(
        selectors.modules,
        (devices) => devices[name]?.activeRequests || {}
      );

    this.selectors = {
      state: createSelector(
        selectors.modules,
        (devices) => devices[name]?.state
      ),
      config: createSelector(
        selectors.modules,
        (devices) => devices[name]?.config
      ),
      info: createSelector(
        selectors.modules,
        (devices) => devices[name]?.info
      ),
      activeRequests,
      isSettingState: createSelector(
        activeRequests,
        (ar) => (ar[MessageName.SetState] ?? 0) > 0
      ),
      isSettingConfig: createSelector(
        activeRequests,
        (ar) => (ar[MessageName.SetConfig] ?? 0) > 0
      ),
    };
  }
  useState(data?: any): VoidFunction {
    if (data && this._statePollRefs)
      throw new Error(
        'state polling with arguments by multiple consumers is not supported'
      );
    this._statePollRefs++;
    this.stateGetData = data;
    return () => {
      if (this._statePollRefs <= 0)
        console.error(
          `esp32m UI bug: ${this.name}.statePollRefs=${this._statePollRefs}`
        );
      else this._statePollRefs--;
      if (this._statePollRefs == 0) delete this.stateGetData;
    };
  }
  isPolling() {
    return this._statePollRefs > 0;
  }
  stateGetData?: any;
  private _statePollRefs = 0;
}

class StatePoller implements IStatePoller {
  constructor(private readonly poller: Periodic) {

  }
  suspend() {
    this._suspendCount++;
    this.update();
    this.poller.disable();
    return () => {
      this._suspendCount++;
      this.poller.disable();
    }
  }
  disable() {
    this._disabled = true;
    this.update();
  }
  enable() {
    this._disabled = false;
    this.update();
  }
  private _suspendCount = 0;
  private _disabled = false;
  private update() {
    const disable = this._disabled || this._suspendCount > 0;
    if (disable != !this.poller.enabled) {
      if (disable) this.poller.disable();
      else this.poller.enable();
    }
  }
}

class Client implements IBackendApi {
  readonly status = new DiscreteStatus(ConnectionStatus.Disconnected);
  readonly incoming = new Subject<TMessage>();
  readonly outgoing = new Subject<TRequest>();
  readonly resolved = new Subject<[TRequest, TResponse]>();
  readonly rejected = new Subject<[TRequest, Error]>();
  readonly statePoller;
  constructor(readonly ws: WebSocket) {
    ws.onopen = () => {
      this.status.set(ConnectionStatus.Connected);
      this.statePoller.enable();
    };
    ws.onclose = () => {
      this.status.set(ConnectionStatus.Connecting);
      this.statePoller.disable();
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
            else if (parsed.error)
              pending.reject(deserializeEsp32mError(parsed.error));
            else pending.resolve(parsed);
        }
      }
    };
    this.statePoller = new StatePoller(new Periodic(async () => {
      const tasks = Object.values(this._devices)
        .filter((d) => d.isPolling())
        .map((d) => this.getState(d.name, d.stateGetData));
      await Promise.allSettled(tasks);
    }, 1000));
  }
  module(name: string): IModuleApi {
    return (
      this._devices[name] || (this._devices[name] = new Module(this, name))
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
    options?: TRequestOptions
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
      type: MessageType.Request,
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
          const error = new Error('timeout')
          this.rejected.next([request, error]);
          rejectFn(error);
        }, options?.timeout || 10000);
      let timer = startTimer();
      resolve = (response) => {
        clearTimeout(timer);
        delete this._pending[capturedSeq];
        this.resolved.next([request, response]);
        resolveFn(response);
      };
      reject = (error) => {
        clearTimeout(timer);
        this.rejected.next([request, error]);
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
    return this.request(target, MessageName.GetState, data);
  }
  setState(target: string, data?: any): Promise<TResponse> {
    return this.request(target, MessageName.SetState, data);
  }
  getConfig(target: string, data?: any): Promise<TResponse> {
    return this.request(target, MessageName.GetConfig, data);
  }
  setConfig(target: string, data?: any): Promise<TResponse> {
    return this.request(target, MessageName.SetConfig, data);
  }
  getInfo(target: string, data?: any): Promise<TResponse> {
    return this.request(target, MessageName.GetInfo, data);
  }

  private readonly flush = debounce(() => {
    const toSend = Object.values(this._pending).reduce((arr, pr) => {
      if (!pr.sent) {
        pr.sent = Date.now();
        this.outgoing.next(pr.request);
        arr.push(pr.request);
      }
      return arr;
    }, [] as Array<TRequest>);
    if (!toSend.length) return;
    this.ws.send(JSON.stringify(toSend));
  }, 100);

  private _seq: number = Math.floor(Math.random() * (Math.pow(2, 31) / 2));
  private readonly _pending: { [key: number]: TPendingRequest } = {};
  private readonly _devices: Record<string, Module> = {};
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
