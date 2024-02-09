import { TPlugin, getPlugins } from '@ts-libs/plugins';
import {
  ISerializableError,
  deserializeError,
  isNumber,
  isPlainObject,
  isString,
  isUndefined,
  memoizeLast,
} from '@ts-libs/tools';
import { Ti18nErrorFormattingOptions } from '@ts-libs/ui-i18n';
import i18next from 'i18next';

export const EspErrorName = 'EspError';

type TEspErrorArgs = Record<string, any>;

type TEspErrorSerial = {
  name: typeof EspErrorName;
  code?: number | string;
  message?: string;
  args?: TEspErrorArgs;
};

type TEspErrorDetails = {
  readonly code?: number | string;
  readonly args?: TEspErrorArgs;
};

type TEspErrorOptions = ErrorOptions & TEspErrorDetails;

const nameByCode = memoizeLast((plugins: TEspErrorPlugin[]) =>
  plugins.reduce(
    (m, p) => (Object.assign(m, p.esp32m?.errors?.nameByCode), m),
    {} as Record<number, string>
  )
);

const decode = (code?: number | string) =>
  isUndefined(code)
    ? undefined
    : isString(code)
      ? code
      : nameByCode(getPlugins<TEspErrorPlugin>())[code] ||
        `ESP_ERR_0x${code.toString(16).padStart(4, '0')}`;

function newEspError(serial: TEspErrorSerial) {
  const { message, args } = serial;
  const code = decode(serial.code);
  return new EspError(message || code || 'general failure', {
    code,
    args,
  });
}

export class EspError
  extends Error
  implements ISerializableError, TEspErrorDetails
{
  readonly name = EspErrorName;
  readonly code?: number | string;
  readonly args?: TEspErrorArgs;
  constructor(message: string, options?: TEspErrorOptions) {
    super(message, options);
    if (options) {
      const { code, args } = options;
      if (code) this.code = code;
      if (args) this.args = args;
    }
  }
  serialize(): TEspErrorSerial {
    const { code, message, args } = this;
    return {
      name: EspErrorName,
      code,
      message,
      args,
    };
  }
  format(options?: Ti18nErrorFormattingOptions): string {
    const { code, message, args } = this;
    const { t } = options?.i18n || i18next;
    const m = code === message ? code : `${code} ${message}`;
    return t(m, args);
  }
  static deserialize(serial: unknown) {
    if (isPlainObject<TEspErrorSerial>(serial) && serial.name === EspErrorName)
      return newEspError(serial);
  }
}

function decodeEspError(error: unknown): unknown {
  const result: Record<string, any> = { name: EspErrorName };
  switch (typeof error) {
    case 'number':
      result.code = error;
      break;
    case 'string':
      result.message = error;
      break;
    case 'object':
      if (Array.isArray(error)) {
        if (error.length == 0) break;
        if (Array.isArray(error[0])) return error.map(decodeEspError);
        const [code, message] = error;
        if (isNumber(code) || isString(code)) result.code = code;
        if (isString(message)) result.message = message;
      } else Object.assign(result, error);
      break;
  }
  return result;
}
export const deserializeEsp32mError = (error: unknown) =>
  deserializeError(decodeEspError(error));

export type TEspErrorPlugin = TPlugin & {
  esp32m: {
    errors: { nameByCode: Record<number, string> };
  };
};
