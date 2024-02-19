export const enum TimeUnit {
  Second = 1,
  Centisecond = 100,
  Millisecond = 1000,
  Microsecond = 1000000,
  Nanosecond = 1000000000,
}

const nanosPerSecBigint = BigInt(TimeUnit.Nanosecond);
let lastNow = 0,
  nowAdj = 0;

function createNow() {
  if (typeof process !== 'undefined') {
    const { hrtime } = process;
    if (hrtime)
      if (typeof hrtime.bigint === 'function')
        return () => TimeValue.fromBigint(hrtime.bigint());
      else if (typeof hrtime === 'function')
        return () => {
          const time = hrtime();
          return new TimeValue(time[0], time[1]);
        };
  }
  if (typeof window !== 'undefined') {
    const { performance } = window;
    if (typeof performance?.now === 'function')
      return () => {
        const { now, timeOrigin } = performance;
        let seconds = ~~(timeOrigin / 1000);
        let millis = now() + (timeOrigin - seconds * 1000);
        if (millis > 1000) {
          const s = ~~(millis / 1000);
          seconds += s;
          millis -= s * 1000;
        }
        return new TimeValue(seconds, ~~(millis * 1000));
      };
  }
  // worst case - use Date.now()
  return () => {
    let time = Date.now() + nowAdj;
    // make sure it is monotonic
    const diff = lastNow - time;
    if (diff > 0) {
      nowAdj += diff;
      time += diff;
    }
    lastNow = time;
    const seconds = ~~(time / 1000);
    return new TimeValue(seconds, (time - seconds * 1000) * 10000000);
  };
}

export class TimeValue {
  constructor(
    private readonly seconds: number,
    private readonly nanos: number
  ) {}
  get isZero() {
    return this.seconds == 0 && this.nanos == 0;
  }
  toNumber(unit: number): number {
    const div = TimeUnit.Nanosecond / unit;
    return ~~(this.nanos / div) + this.seconds * unit;
  }
  toBigint(unit: number = TimeUnit.Nanosecond) {
    if (unit === TimeUnit.Nanosecond)
      return BigInt(this.nanos) + BigInt(this.seconds) * nanosPerSecBigint;
    const div = TimeUnit.Nanosecond / unit;
    return (
      BigInt(this.nanos) / BigInt(div) + BigInt(this.seconds) * BigInt(unit)
    );
  }
  plus(other: TimeValue) {
    let seconds = this.seconds + other.seconds;
    let nanos = this.nanos + other.nanos;
    if (nanos > TimeUnit.Nanosecond) {
      seconds++;
      nanos -= TimeUnit.Nanosecond;
    }
    return new TimeValue(seconds, nanos);
  }
  add(value: number, unit: number) {
    const other = TimeValue.from(value, unit);
    return this.plus(other);
  }
  minus(other: TimeValue) {
    let { seconds, nanos } = other;
    if (this.nanos < nanos) {
      const nsec = ~~((nanos - this.nanos) / TimeUnit.Nanosecond) + 1;
      nanos -= TimeUnit.Nanosecond * nsec;
      seconds += nsec;
    }
    const d = this.nanos - nanos;
    if (d > TimeUnit.Nanosecond) {
      const nsec = ~~(d / TimeUnit.Nanosecond);
      nanos += TimeUnit.Nanosecond * nsec;
      seconds -= nsec;
    }
    return new TimeValue(this.seconds - seconds, this.nanos - nanos);
  }
  subtract(value: number, unit: number) {
    const other = TimeValue.from(value, unit);
    return this.minus(other);
  }
  compare(other: TimeValue) {
    return this.seconds == other.seconds
      ? this.nanos == other.nanos
        ? 0
        : this.nanos < other.nanos
          ? -1
          : 1
      : this.seconds < other.seconds
        ? -1
        : 1;
  }
  equals(other: TimeValue) {
    return this.seconds == other.seconds && this.nanos == other.nanos;
  }
  static fromBigint(time: bigint, unit: number = TimeUnit.Nanosecond) {
    const d = BigInt(unit);
    return new TimeValue(Number(time / d), Number(time % d));
  }
  static from(time: number, unit: number = TimeUnit.Nanosecond) {
    return new TimeValue(~~(time / unit), ~~(time % unit));
  }
  static readonly zero = new TimeValue(0, 0);
  static readonly now = createNow();
}
