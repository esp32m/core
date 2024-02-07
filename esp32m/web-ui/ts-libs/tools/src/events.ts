import {
  filter,
  first,
  firstValueFrom,
  Observable,
  Subject,
  Subscription,
} from 'rxjs';
import { isThenable } from './is-type';
import { Mutable } from './types';
import { CompositeError } from './errors';

export type TEvent<T extends string, P = unknown> = {
  readonly type: T;
  readonly tasks?: Array<Promise<P>>;
  readonly errors?: Array<Error>;
};

export interface IEvents<T extends string, EI extends Record<T, TEvent<T>>> {
  get observable(): Observable<EI[T]>;
  fire<K extends T>(ev: { type: K } & EI[K]): void;
  fireAsync<K extends T>(ev: EI[K]): Promise<unknown[]>;
  subscribe<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription;
  subscribeAsync<K extends T>(
    type: K,
    fn: (value: EI[K]) => Promise<void>
  ): Subscription;
  once<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription;
  observe<K extends T>(type: K): Observable<EI[K]>;
  when<K extends T>(type: K): Promise<EI[K]>;
}

export class Events<T extends string, EI extends Record<T, TEvent<T>>>
  implements IEvents<T, EI>
{
  private readonly _subject = new Subject<EI[T]>();
  get observable(): Observable<EI[T]> {
    return this._subject;
  }
  fire<K extends T>(ev: { type: K } & EI[K]) {
    this._subject.next(ev);
    this.handleErrors(ev);
  }
  fireAsync<K extends T, P = unknown>(ev: EI[K]): Promise<P[]> {
    const tasks = (ev.tasks || ((ev as any).tasks = [])) as Promise<P>[];
    this._subject.next(ev);
    this.handleErrors(ev);
    return Promise.all(tasks);
  }
  subscribe<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription {
    return this._subject
      .pipe(filter((e) => e.type == type))
      .subscribe(this.wrapHander(fn));
  }
  subscribeAsync<K extends T>(
    type: K,
    fn: (value: EI[K]) => Promise<void>
  ): Subscription {
    return this._subject.pipe(filter((e) => e.type == type)).subscribe((e) => {
      const promise = this.wrapHander(fn)(e as EI[K]);
      if (isThenable(promise) && e.tasks) e.tasks.push(promise);
    });
  }
  once<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription {
    return this._subject
      .pipe(
        filter((e) => e.type == type),
        first()
      )
      .subscribe(this.wrapHander(fn));
  }
  observe<K extends T>(type: K): Observable<EI[K]> {
    return this._subject.pipe(filter((e) => e.type == type)) as Observable<
      EI[K]
    >;
  }
  when<K extends T>(type: K): Promise<EI[K]> {
    return firstValueFrom(
      this._subject.pipe(filter((e) => e.type == type)) as Observable<EI[K]>
    );
  }
  private handleErrors<K extends T>(ev: { type: K } & EI[K]) {
    const l = ev.errors?.length;
    if (!l) return;
    /*if (l == 1) throw ev.errors[0];
    else if (l > 1) */
    throw new CompositeError(ev.errors);
  }
  private wrapHander =
    <K extends T>(fn: (value: EI[K]) => unknown) =>
    (event: Mutable<EI[T]>) => {
      try {
        return fn(event as EI[K]);
      } catch (e) {
        if (!event.errors) event.errors = [];
        event.errors.push(e);
      }
    };
}
