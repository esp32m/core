import type { TJsonValue } from '@ts-libs/tools';
import type {
  TResponse,
  TStatePatch,
  TStateSnapshot,
  TStateUpdate,
  TStateView,
} from './types';

// Object key order must not create duplicate subscriptions for the same query.
export const canonicalQuery = (query?: any): string => {
  const sort = (value: any): any =>
    Array.isArray(value)
      ? value.map(sort)
      : value && typeof value === 'object'
        ? Object.fromEntries(
            Object.keys(value)
              .sort()
              .map((key) => [key, sort(value[key])])
          )
        : value;
  return query == null ? '' : JSON.stringify(sort(query));
};

export const stateViewKey = (query?: any, view: TStateView = 'state') =>
  view === 'state' && query == null
    ? 'state'
    : `${view}:${canonicalQuery(query)}`;

const unsafeKey = (key: string | number) =>
  key === '__proto__' || key === 'prototype' || key === 'constructor';

// Apply atomically: an invalid patch cannot leave a half-updated cached state.
export const applyStatePatch = (
  state: TJsonValue,
  patch: TStatePatch
): TJsonValue => {
  let result = state;
  for (const operation of patch) {
    const { op, path } = operation;
    if (
      (op !== 'set' && op !== 'remove') ||
      !Array.isArray(path) ||
      path.length > 64 ||
      path.some(
        (key) =>
          (typeof key !== 'string' && typeof key !== 'number') || unsafeKey(key)
      )
    )
      throw new Error('Invalid state patch');
    if (
      op === 'set' &&
      !Object.prototype.hasOwnProperty.call(operation, 'value')
    )
      throw new Error('Missing state patch value');
    const apply = (value: any, depth: number): any => {
      if (depth === path.length) {
        if (op === 'remove') throw new Error('Cannot remove state root');
        return operation.value;
      }
      if (value === null || typeof value !== 'object')
        throw new Error('Missing state patch parent');
      const key = path[depth];
      const last = depth === path.length - 1;
      if (Array.isArray(value)) {
        if (
          !Number.isInteger(key) ||
          Number(key) < 0 ||
          Number(key) >= value.length
        )
          throw new Error('Invalid state patch array index');
        const copy = value.slice();
        if (last && op === 'remove') copy.splice(Number(key), 1);
        else copy[Number(key)] = apply(value[Number(key)], depth + 1);
        return copy;
      }
      if (
        typeof key !== 'string' ||
        (!last && !Object.prototype.hasOwnProperty.call(value, key))
      )
        throw new Error('Invalid state patch object key');
      const copy = { ...value };
      if (last && op === 'remove') delete copy[key];
      else copy[key] = apply(value[key], depth + 1);
      return copy;
    };
    result = apply(result, 0);
  }
  return result;
};

type Interest = {
  target: string;
  key: string;
  view: TStateView;
  query?: any;
  refs: number;
  generation: number;
  operation: number;
  id?: string;
  stream?: string;
  rev?: number;
  state?: TJsonValue;
  live: boolean;
  retry?: ReturnType<typeof setTimeout>;
  failures: number;
  blocked?: boolean;
  error?: string;
  registered?: boolean;
};

type Send = (
  name: string,
  data: any,
  receive?: (response: TResponse) => void
) => Promise<TResponse>;

/** Browser-side ownership and revision tracking. No timer polls state. */
export class StateSubscriptions {
  constructor(
    private readonly send: Send,
    private readonly publish: (update: TStateUpdate) => void
  ) {}

  get active() {
    return (
      this.enabled &&
      [...this.interests.values()].some((interest) => !interest.blocked)
    );
  }

  watch(target: string, query?: any, view: TStateView = 'state'): VoidFunction {
    const identity = this.identity(target, query, view);
    const interest = this.interest(target, query, view, true)!;
    if (++interest.refs === 1 && this.enabled)
      void this.subscribe(interest).catch(() => undefined);
    let released = false;
    return () => {
      if (released) return;
      released = true;
      if (--interest.refs === 0) {
        this.invalidate(interest, true);
        this.interests.delete(identity);
      }
    };
  }

  owns(target: string, query?: any, view: TStateView = 'state') {
    return !!this.interest(target, query, view)?.refs;
  }

  async refresh(
    target: string,
    query?: any,
    view: TStateView = 'state'
  ): Promise<TResponse> {
    const interest = this.interest(target, query, view);
    if (!interest || !this.enabled)
      throw new Error('State subscription is not connected');
    const initial = !interest.id;
    const pending = initial
      ? this.subscribe(interest)
      : this.snapshot(interest, 'state-resync');
    const generation = interest.generation;
    const response = await pending.catch((error) => {
      if (!initial) this.retry(interest, generation, error);
      throw error;
    });
    if (generation !== interest.generation || !interest.live)
      throw new Error('State subscription changed during refresh');
    return {
      ...response,
      name: view === 'device' ? 'device-state' : 'state-get',
      source: target,
      data: interest.state,
    };
  }

  setConnected(connected: boolean) {
    if (this.connected === connected) return;
    this.connected = connected;
    if (connected)
      for (const interest of this.interests.values()) interest.blocked = false;
    this.update();
  }

  suspend() {
    ++this.suspended;
    this.update();
    let released = false;
    return () => {
      if (released) return;
      released = true;
      --this.suspended;
      this.update();
    };
  }

  receive(source: string, data: any) {
    if (
      !data ||
      typeof data.stream !== 'string' ||
      !Number.isSafeInteger(data.rev) ||
      data.rev < 0
    )
      return;
    for (const interest of this.interests.values()) {
      if (
        !interest.live ||
        interest.target !== source ||
        interest.stream !== data.stream
      )
        continue;
      if (data.rev <= (interest.rev ?? -1)) continue;
      try {
        if (Object.prototype.hasOwnProperty.call(data, 'state'))
          interest.state = data.state;
        else if (data.baseRev === interest.rev && Array.isArray(data.patch))
          interest.state = applyStatePatch(interest.state!, data.patch);
        else throw new Error('State revision gap');
        interest.rev = data.rev;
        this.emit(interest, true);
      } catch {
        interest.live = false;
        this.emit(interest);
        if (!this.resyncing.has(interest)) {
          this.resyncing.add(interest);
          const generation = interest.generation;
          void this.snapshot(interest, 'state-resync')
            .catch((error) => this.retry(interest, generation, error))
            .finally(() => this.resyncing.delete(interest));
        }
      }
    }
  }

  private readonly interests = new Map<string, Interest>();
  private readonly resyncing = new Set<Interest>();
  private connected = false;
  private suspended = 0;
  private serial = 0;
  private get enabled() {
    return this.connected && !this.suspended;
  }
  private identity(target: string, query?: any, view: TStateView = 'state') {
    return JSON.stringify([target, stateViewKey(query, view)]);
  }
  private interest(
    target: string,
    query?: any,
    view: TStateView = 'state',
    create = false
  ) {
    const identity = this.identity(target, query, view);
    let interest = this.interests.get(identity);
    if (!interest && create) {
      const canonical = canonicalQuery(query);
      interest = {
        target,
        key: stateViewKey(query, view),
        view,
        query: canonical === '' ? undefined : JSON.parse(canonical),
        refs: 0,
        generation: 0,
        operation: 0,
        live: false,
        failures: 0,
      };
      this.interests.set(identity, interest);
    }
    return interest;
  }
  private emit(interest: Interest, received = false) {
    this.publish({
      target: interest.target,
      key: interest.key,
      state: interest.state,
      live: interest.live,
      stream: interest.stream,
      rev: interest.rev,
      error: interest.error,
      received,
    });
  }
  private update() {
    for (const interest of this.interests.values()) {
      if (!this.enabled) this.invalidate(interest, this.connected);
      else if (!interest.id && !interest.blocked)
        void this.subscribe(interest).catch(() => undefined);
    }
  }
  private invalidate(interest: Interest, unsubscribe: boolean) {
    clearTimeout(interest.retry);
    interest.retry = undefined;
    const id = interest.id;
    ++interest.generation;
    delete interest.id;
    delete interest.stream;
    delete interest.rev;
    interest.registered = false;
    interest.live = false;
    this.emit(interest);
    if (unsubscribe && id)
      void this.send('state-unsubscribe', { id }).catch(() => undefined);
  }
  private subscribe(interest: Interest) {
    clearTimeout(interest.retry);
    interest.retry = undefined;
    interest.blocked = false;
    interest.error = undefined;
    interest.id = `s${++this.serial}`;
    ++interest.generation;
    const generation = interest.generation;
    this.emit(interest);
    return this.snapshot(interest, 'state-subscribe').catch((error) => {
      this.retry(interest, generation, error);
      throw error;
    });
  }
  private snapshot(interest: Interest, name: string) {
    const { generation, id } = interest;
    const operation = ++interest.operation;
    const data =
      name === 'state-subscribe'
        ? {
            id,
            target: interest.target,
            view: interest.view,
            ...(interest.query === undefined ? {} : { query: interest.query }),
          }
        : { id };
    return this.send(name, data, (response) => {
      if (
        response.error ||
        generation !== interest.generation ||
        operation !== interest.operation ||
        !interest.refs
      )
        return;
      const snapshot = response.data as TStateSnapshot;
      if (
        !snapshot ||
        snapshot.id !== id ||
        snapshot.source !== interest.target ||
        typeof snapshot.stream !== 'string' ||
        !Number.isSafeInteger(snapshot.rev) ||
        snapshot.rev < 0 ||
        !Object.prototype.hasOwnProperty.call(snapshot, 'state')
      )
        return;
      if (
        snapshot.stream === interest.stream &&
        snapshot.rev < (interest.rev ?? -1)
      )
        return;
      interest.stream = snapshot.stream;
      interest.rev = snapshot.rev;
      interest.state = snapshot.state;
      interest.live = true;
      interest.registered = true;
      interest.error = undefined;
      interest.failures = 0;
      this.emit(interest, true);
    }).then((response) => {
      if (
        generation === interest.generation &&
        operation === interest.operation &&
        interest.refs &&
        !interest.live
      )
        throw new Error('Invalid state subscription snapshot');
      return response;
    });
  }
  private retry(interest: Interest, generation: number, error?: any) {
    if (!this.enabled || !interest.refs || generation !== interest.generation)
      return;
    const code = String(
      error?.code || error?.message || 'State subscription unavailable'
    );
    const terminal = [
      'unsupported-state',
      'invalid-subscription',
      'subscription-conflict',
    ].includes(code);
    interest.error = code;
    // A permanent rejection of a new subscription did not register an interest.
    // For timeouts the outcome is unknown, so unsubscribe before retrying.
    this.invalidate(interest, !terminal || !!interest.registered);
    if (terminal) {
      interest.blocked = true;
      return;
    }
    // Failed subscriptions may recover (for example after resource pressure).
    // This retries subscription establishment only, never reads on a timer.
    interest.retry = setTimeout(
      () => {
        interest.retry = undefined;
        if (this.enabled && interest.refs && !interest.id)
          void this.subscribe(interest).catch(() => undefined);
      },
      Math.min(30_000, 1000 * 2 ** Math.min(interest.failures++, 5))
    );
  }
}
