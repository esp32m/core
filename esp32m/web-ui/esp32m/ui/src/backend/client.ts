import {
  DiscreteStatus,
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
  TStateView,
  TStateUpdate,
  TStateEvent,
} from './types';
import { Subject } from 'rxjs';
import WebSocket from 'reconnecting-websocket';
import { selectors } from './state';
import { createSelector } from '@reduxjs/toolkit';
import { deserializeEsp32mError } from './errors';
import { StateSubscriptions, stateViewKey } from './subscriptions';
import { ServerClock } from './clock';

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
  receive?: (response: TResponse) => void;
};

export function isResponse(msg: TMessage): msg is TResponse {
  return msg.type === 'response' && isString((msg as TResponse).source);
}
export function isBroadcast(msg: TMessage): msg is TBroadcast {
  return msg.type === 'broadcast' && isString((msg as TBroadcast).source);
}
export function isStateEvent(msg: TMessage): msg is TStateEvent {
  return (
    msg.type === MessageType.Event &&
    (msg as TStateEvent).name === 'state-changed' &&
    isString((msg as TStateEvent).source)
  );
}

class Module implements IModuleApi {
  readonly selectors;
  constructor(
    readonly api: Client,
    readonly name: string
  ) {
    const activeRequests = createSelector(
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
      info: createSelector(selectors.modules, (devices) => devices[name]?.info),
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
  useState(data?: any, view: TStateView = 'state'): VoidFunction {
    return this.api.subscriptions.watch(this.name, data, view);
  }
  stateSelector(data?: any, view: TStateView = 'state') {
    const key = stateViewKey(data, view);
    return (state: Parameters<typeof selectors.modules>[0]) =>
      key === 'state'
        ? this.selectors.state(state)
        : selectors.modules(state)[this.name]?.views?.[key]?.state;
  }
  liveSelector(data?: any, view: TStateView = 'state') {
    const key = stateViewKey(data, view);
    return (state: Parameters<typeof selectors.modules>[0]) =>
      selectors.modules(state)[this.name]?.views?.[key]?.live === true;
  }
  errorSelector(data?: any, view: TStateView = 'state') {
    const key = stateViewKey(data, view);
    return (state: Parameters<typeof selectors.modules>[0]) =>
      selectors.modules(state)[this.name]?.views?.[key]?.error;
  }
  epochSelector(data?: any, view: TStateView = 'state') {
    const key = stateViewKey(data, view);
    return (state: Parameters<typeof selectors.modules>[0]) => selectors.modules(state)[this.name]?.views?.[key]?.epoch;
  }
}

export class Client implements IBackendApi {
  readonly status = new DiscreteStatus(ConnectionStatus.Disconnected);
  readonly incoming = new Subject<TMessage>();
  readonly outgoing = new Subject<TRequest>();
  readonly resolved = new Subject<[TRequest, TResponse]>();
  readonly rejected = new Subject<[TRequest, Error]>();
  readonly stateUpdates = new Subject<TStateUpdate>();
  readonly clock = new ServerClock();
  readonly subscriptions: StateSubscriptions;
  readonly statePoller;
  constructor(readonly ws: WebSocket) {
    this.subscriptions = new StateSubscriptions(
      (name, data, receive) =>
        this.requestInternal('ui', name, data, undefined, receive, true),
      (update) => this.stateUpdates.next({ ...update, epoch: update.received ? this.clock.generation : undefined })
    );
    // Keep the upload suspension API; its lifetime now controls subscriptions.
    this.statePoller = { suspend: () => this.subscriptions.suspend() };
    ws.onopen = () => {
      this.clock.reset();
      this.status.set(ConnectionStatus.Connected);
      this.subscriptions.setConnected(true);
      this.flushNow();
      this.startHeartbeat();
    };
    ws.onclose = () => {
      this.status.set(ConnectionStatus.Connecting);
      this.subscriptions.setConnected(false);
      this.clock.reset();
      ++this.connection;
      clearTimeout(this.heartbeatTimer);
      for (const pending of Object.values(this._pending))
        pending.reject(new Error('disconnected'));
    };
    ws.onerror = (e: any) => {
      this.status.set(ConnectionStatus.Connecting, serializeError(e));
    };
    ws.onmessage = (msg: MessageEvent) => {
      let parsed: TMessage;
      try {
        parsed = JSON.parse(msg.data);
      } catch {
        return;
      }
      if (parsed) {
        this.clock.observe(parsed.serverTimeMs ?? parsed.data?.serverTimeMs);
        if (isStateEvent(parsed))
          this.subscriptions.receive(parsed.source, parsed.data);
        if (isResponse(parsed)) {
          const pending =
            parsed.seq === undefined ? undefined : this._pending[parsed.seq];
          // Register streams synchronously before the next WS event can arrive.
          if (pending && !parsed.partial && !parsed.error) {
            pending.receive?.(parsed);
            this.cacheRead(pending.request, parsed);
          }
          if (isString(parsed.type)) this.incoming.next(parsed);
          if (pending)
            if (parsed.partial) pending.partial(parsed);
            else if (parsed.error)
              pending.reject(deserializeEsp32mError(parsed.error));
            else pending.resolve(parsed);
        } else if (isString(parsed.type)) this.incoming.next(parsed);
      }
    };
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
    return this.requestInternal(target, name, data, options);
  }
  private requestInternal(
    target: string,
    name: string,
    data?: any,
    options?: TRequestOptions,
    receive?: (response: TResponse) => void,
    immediate = false
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
          const error = new Error('timeout');
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
      receive,
    };
    if (immediate) this.flushNow();
    else this.flush();
    return promise;
  }
  getState(target: string, data?: any): Promise<TResponse> {
    if (this.subscriptions.owns(target, data))
      return this.refreshState(target, data);
    return this.request(target, MessageName.GetState, data);
  }
  refreshState(
    target: string,
    data?: any,
    view: TStateView = 'state'
  ): Promise<TResponse> {
    if (this.subscriptions.owns(target, data, view))
      return this.subscriptions.refresh(target, data, view);
    return this.request(
      target,
      view === 'device' ? 'device-state' : MessageName.GetState,
      data
    );
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

  private cacheRead(request: TRequest, response: TResponse) {
    const { target, name, data } = request;
    if (!target) return;
    const view = name === 'device-state' ? 'device' : 'state';
    if (
      name !== MessageName.GetState &&
      name !== MessageName.SetState &&
      name !== 'device-state'
    )
      return;
    const query = name === MessageName.SetState ? undefined : data;
    if (
      this.subscriptions.owns(target, query, view) ||
      response.data === undefined
    )
      return;
    this.stateUpdates.next({
      target,
      key: stateViewKey(query, view),
      live: false,
      state: response.data,
      epoch: this.clock.generation,
    });
  }

  private flushNow() {
    if (this.status.value !== ConnectionStatus.Connected) return;
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
  }
  private readonly flush = debounce(() => this.flushNow(), 100);

  private connection = 0;
  private heartbeatTimer?: ReturnType<typeof setTimeout>;
  private startHeartbeat() {
    clearTimeout(this.heartbeatTimer);
    const connection = ++this.connection;
    const check = async () => {
      if (
        connection !== this.connection ||
        this.status.value !== ConnectionStatus.Connected
      )
        return;
      try {
        if (this.subscriptions.active)
          await this.requestInternal(
            'ui',
            'ping',
            undefined,
            { timeout: 10_000 },
            undefined,
            true
          );
      } catch {
        if (connection === this.connection && this.subscriptions.active) {
          this.status.set(ConnectionStatus.Connecting);
          this.subscriptions.setConnected(false);
          this.ws.reconnect();
          return;
        }
      }
      if (connection === this.connection)
        this.heartbeatTimer = setTimeout(check, 15_000);
    };
    this.heartbeatTimer = setTimeout(check, 15_000);
  }

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
