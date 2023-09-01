import { isNumber, isFunction } from '@ts-libs/tools';

export const rssiToStr = (rssi: number): string =>
  `${rssi} dBm (${Math.min(Math.round(2 * (rssi + 100)), 100)}%)`;

export const rssiToLevel = (rssi: number, numLevels: number): number => {
  const MIN_RSSI = -100.0;
  const MAX_RSSI = -55.0;
  if (rssi <= MIN_RSSI) {
    return 0;
  } else if (rssi >= MAX_RSSI) {
    return numLevels - 1;
  } else {
    const inputRange = MAX_RSSI - MIN_RSSI;
    const outputRange = numLevels - 1;
    return Math.trunc(((rssi - MIN_RSSI) * outputRange) / inputRange);
  }
};

export const isFiniteNumber = (value: unknown) =>
  isNumber(value) && isFinite(value);

export const netmask2cidr = (mask: string) => {
  const maskNodes = mask.match(/(\d+)/g);
  let cidr = 0;
  for (const i in maskNodes)
    cidr += ((Number(maskNodes[Number(i)]) >>> 0).toString(2).match(/1/g) || [])
      .length;
  return cidr;
};

export function resolveFunction<T>(
  v: T | ((...args: unknown[]) => T),
  ...args: unknown[]
): T {
  while (isFunction(v)) v = v(...args);
  return v;
}

export function formatBytes(bytes: number, decimals = 2): string {
  if (bytes === 0) return '0 Bytes';

  const k = 1024;
  const dm = decimals < 0 ? 0 : decimals;
  const sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'];

  const i = Math.floor(Math.log(bytes) / Math.log(k));

  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
}

// credits: adapted from https://stackoverflow.com/questions/8211744/convert-time-interval-given-in-seconds-into-more-human-readable-form

export function millisToStr(milliseconds: number): string {
  function numberEnding(number: number) {
    return number > 1 ? 's' : '';
  }

  let temp = Math.floor(milliseconds / 1000);
  const years = Math.floor(temp / 31536000);
  if (years) return years + ' year' + numberEnding(years);

  const days = Math.floor((temp %= 31536000) / 86400);
  if (days) return days + ' day' + numberEnding(days);

  const hours = Math.floor((temp %= 86400) / 3600);
  if (hours) return hours + ' hour' + numberEnding(hours);

  const minutes = Math.floor((temp %= 3600) / 60);
  if (minutes) return minutes + ' minute' + numberEnding(minutes);

  const seconds = temp % 60;
  if (seconds) return seconds + ' second' + numberEnding(seconds);

  return 'less than a second';
}

const defines: Record<string, any> = {};

export function setDefine(name: string, value: any) {
  defines[name] = value;
}

export function getDefine(name: string) {
  return defines[name];
}

export function getDefines(names: Array<string>) {
  return names.map((n) => defines[n]);
}
