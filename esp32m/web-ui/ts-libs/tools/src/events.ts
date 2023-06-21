import {
  concatMap,
  filter,
  first,
  firstValueFrom,
  Observable,
  Subject,
  Subscription,
} from 'rxjs';
import { isThenable } from './is-type';

export type TEvent<T extends string, P = unknown> = {
  readonly type: T;
  readonly tasks?: Array<Promise<P>>;
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
  }
  fireAsync<K extends T, P = unknown>(ev: EI[K]): Promise<P[]> {
    const tasks = (ev.tasks || ((ev as any).tasks = [])) as Promise<P>[];
    this._subject.next(ev);
    return Promise.all(tasks);
  }
  subscribe<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription {
    return this._subject
      .pipe(filter((e) => e.type == type))
      .subscribe(fn as any);
  }
  subscribeAsync<K extends T>(
    type: K,
    fn: (value: EI[K]) => Promise<void>
  ): Subscription {
    return this._subject
      .pipe(
        filter((e) => e.type == type),
        concatMap((e) => {
          const promise = fn(e as EI[K]);
          if (isThenable(promise) && e.tasks) e.tasks.push(promise);
          return promise;
        })
      )
      .subscribe();
  }
  once<K extends T>(type: K, fn: (value: EI[K]) => void): Subscription {
    return this._subject
      .pipe(
        filter((e) => e.type == type),
        first()
      )
      .subscribe(fn as any);
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
}
