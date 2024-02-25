import { TPlugin, getPlugins } from '@ts-libs/plugins';
import { isFunction, isPrimitive, isString, isUndefined } from './is-type';

export type TErrorFormattingOptions = {
  stack: boolean;
};

export interface IFormattableError extends Error {
  format(options?: TErrorFormattingOptions): string;
}

export function errorToString(
  e: any,
  options?: TErrorFormattingOptions
): string | undefined {
  if (isUndefined(e)) return;
  if (!e) return 'error';
  if (isPrimitive(e)) return e.toString();
  if (e instanceof CompositeError)
    return e.errors.map((e) => errorToString(e, options)).join(';');
  if (e instanceof Error && isFunction((e as IFormattableError).format))
    return (e as IFormattableError).format(options);
  const { message, name, type, text, title, stack } = e;
  if (options?.stack && stack) return stack; // stack usually includes error message
  return message || text || title || name || type || e.toString();
}

function isSerializableError(e: any): e is ISerializableError {
  return e instanceof Error && isFunction((e as ISerializableError).serialize);
}

export function serializeError(e: any) {
  if (isUndefined(e)) return;
  if (!e) return 'error';
  if (isPrimitive(e)) return e.toString();
  if (isSerializableError(e)) return e.serialize();
  const result = ['message', 'name', 'type', 'text', 'title', 'code'].reduce(
    (m, k) => {
      const v = e[k];
      if (v != null && isPrimitive(v)) m[k] = v;
      return m;
    },
    {} as Record<string, any>
  );
  if (e instanceof Error) {
    if (e.cause) result.options = { cause: serializeError(e.cause) };
    if (isString(e.stack)) result.stack = e.stack.split(/\r?\n/);
  }
  return result;
}

export type ErrorDeserializer = (serial: any) => Error | undefined;

export interface ISerializableError extends Error {
  serialize(): unknown;
}
export type TErrorDeserializerPlugin = TPlugin & {
  errors: {
    deserializer: ErrorDeserializer;
  };
};

function deserializeStandardError(serial: any) {
  if (typeof serial === 'object' && serial !== null)
    switch (serial.name) {
      case 'Error':
      case 'InternalError':
        return new Error(serial.message, serial.options);
      case 'RangeError':
        return new RangeError(serial.message, serial.options);
      case 'ReferenceError':
        return new ReferenceError(serial.message, serial.options);
      case 'SyntaxError':
        return new SyntaxError(serial.message, serial.options);
      case 'TypeError':
        return new TypeError(serial.message, serial.options);
      case 'URIError':
        return new URIError(serial.message, serial.options);
      case 'EvalError':
        return new EvalError(serial.message, serial.options);
      case 'AssertErrorName':
        return new AssertError(serial.message, serial.options);
    }
}

export const CompositeErrorName = 'CompositeError';
export class CompositeError extends Error {
  readonly name = CompositeErrorName;
  constructor(readonly errors: Array<Error>) {
    super(errors.map((e) => `[${e.name}] ${e.message}`).join(';'));
  }
}

export const deserializeError: ErrorDeserializer = (serial) => {
  if (serial instanceof Error) return serial;
  if (Array.isArray(serial)) {
    const errors = serial
      .map(deserializeError)
      .filter((e) => e instanceof Error);
    if (errors.length == 0) return new Error();
    if (errors.length == 1) return errors[0];
    return new CompositeError(errors as Error[]);
  }
  if (!serial) return new Error();
  const { stack, ...rest } = serial;
  let result = deserializeStandardError(rest);
  if (!result) {
    const deserializers = getPlugins<TErrorDeserializerPlugin>()
      .map((p) => p.errors?.deserializer)
      .filter((d) => !!d);
    for (const d of deserializers) {
      const e = d(serial);
      if (e) {
        result = e;
        break;
      }
    }
  }
  if (!result) result = new Error(JSON.stringify(rest));
  if (stack) (result as any).remoteStack = stack;
  return result;
};

export const AssertErrorName = 'AssertError';

export class AssertError extends Error {
  constructor(
    readonly message: string,
    options?: ErrorOptions
  ) {
    super(`assertion failed${message ? `: ${message}` : ''}`, options);
    this.name = AssertErrorName;
  }
}

export function assert(condition: boolean, message?: string) {
  if (!condition) throw new AssertError(message || '');
}
