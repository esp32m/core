import {
  filter,
  firstValueFrom,
  Observable,
  Subject,
  Subscription,
} from 'rxjs';

type TStatusChangedCallback<T> = (
  state: DiscreteStatus<T>,
  old?: T
) => void | Promise<void>;

export class DiscreteStatus<T> {
  readonly changed: Observable<T>;
  details: any;
  constructor(initial: T) {
    this._value = initial;
    this.changed = new Subject<T>();
  }
  get value() {
    return this._value;
  }
  is(value: T) {
    return value === this._value;
  }
  any(...values: T[]) {
    return values.includes(this._value);
  }
  set(value: T, details?: any) {
    this.details = details;
    if (value === this._value) return false;
    const prev = this._value;
    this._prev = prev;
    this._value = value;
    (this.changed as Subject<T>).next(prev);
  }
  /**
   * @summary Waits for specific status, then calls the provided callback and resolves the returned promise.
   * If status matches at the time of the call, callback is called and promise is resoved immediately.
   **/
  when(value: T | T[], cb?: TStatusChangedCallback<T>): Promise<T | void> {
    const tester = Array.isArray(value)
      ? () => this.any(...value)
      : () => this.is(value);
    return new Promise((resolve, reject) => {
      if (tester()) {
        if (cb)
          Promise.resolve(cb(this, this._prev)).then(resolve).catch(reject);
        else resolve(this._prev);
      } else
        firstValueFrom(this.changed.pipe(filter(tester))).then(async (v) => {
          if (cb) await Promise.resolve(cb(this, v));
          resolve(v);
        });
    });
  }
  whenever(
    value: T | T[],
    cb: TStatusChangedCallback<T>,
    now?: boolean
  ): Subscription {
    const tester = Array.isArray(value)
      ? () => this.any(...value)
      : () => this.is(value);
    if (now && tester()) cb(this, this._prev);
    return this.changed.pipe(filter(tester)).subscribe((v) => cb(this, v));
  }
  /**
   * @summary Invokes provided callback immediately with the current status, and then every time the status changes.
   * @returns subscription
   */
  pass(cb: TStatusChangedCallback<T>) {
    cb(this);
    return this.changed.subscribe((v) => cb(this, v));
  }
  private _value: T;
  private _prev?: T;
}
