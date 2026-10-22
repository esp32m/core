import type { IBackendApi } from '../../backend';
import type { LogConfig, LogInfo, LogList, LogPage, LogState } from './types';
import { Name } from './types';

export const MaxPageBytes = 8192;
export const MaxCatalogFiles = 128;
export const MaxAgeDays = 3650;
export const PercentUnit = 0;

export function quotaBaseBytes(info: LogInfo, state?: LogState): number {
  return (
    [
      state?.quotaBaseBytes,
      info.quotaBaseBytes,
      state ? state.freeBytes + state.usedBytes : undefined,
      info.freeBytes,
    ].find((value) => Number.isSafeInteger(value) && value! >= 0) ?? 0
  );
}

export function quotaPreview(
  quota: string,
  unit: number,
  info: LogInfo,
  state?: LogState
) {
  if (!quota.trim() || ![PercentUnit, 1024, 1048576].includes(unit))
    return null;
  const amount = Number(quota);
  const baseBytes = quotaBaseBytes(info, state);
  const quotaPercent = unit === PercentUnit ? amount : 0;
  if (
    unit === PercentUnit &&
    (!info.supportsQuotaPercent ||
      !Number.isSafeInteger(amount) ||
      amount < 1 ||
      amount > 100)
  )
    return null;
  const quotaBytes =
    unit === PercentUnit
      ? Math.floor((baseBytes * amount) / 100)
      : amount * unit;
  if (!Number.isSafeInteger(quotaBytes) || quotaBytes < 0) return null;
  const effectiveQuotaBytes = Math.max(
    0,
    Math.min(quotaBytes, info.maxQuotaBytes, baseBytes - info.reserveBytes)
  );
  return { baseBytes, quotaPercent, quotaBytes, effectiveQuotaBytes };
}

export function convertQuota(
  quota: string,
  from: number,
  to: number,
  baseBytes: number
) {
  if (!quota.trim() || !Number.isFinite(Number(quota))) return quota;
  const bytes =
    from === PercentUnit
      ? Math.floor((baseBytes * Number(quota)) / 100)
      : Number(quota) * from;
  if (to === PercentUnit) {
    if (baseBytes <= 0) return '';
    return String(
      bytes <= 0
        ? 0
        : Math.max(1, Math.min(100, Math.round((bytes * 100) / baseBytes)))
    );
  }
  return String(bytes / to);
}

export async function discoverLogs(api: IBackendApi): Promise<LogInfo | null> {
  // Old firmware ignores requests addressed to an unknown module. Its app
  // descriptor is a bounded, supported way to avoid those request timeouts.
  const { data: modules } = await api.request('app', 'describe');
  if (!modules || !Object.prototype.hasOwnProperty.call(modules, Name))
    return null;
  const { data: info } = await api.getInfo(Name);
  if (info?.schema !== 1) return null;
  return info;
}

export function pageBytes(info: LogInfo) {
  return Math.max(
    1,
    Math.min(
      info.pageBytes || 4096,
      info.maxPageBytes || MaxPageBytes,
      MaxPageBytes
    )
  );
}

export function checkPage(page: LogPage, id: string): LogPage {
  if (
    !page ||
    page.id !== id ||
    typeof page.generation !== 'string' ||
    typeof page.text !== 'string' ||
    ![page.offset, page.end, page.size].every(Number.isSafeInteger) ||
    page.offset < 0 ||
    page.end < page.offset ||
    page.size < page.end ||
    page.end - page.offset > MaxPageBytes ||
    new TextEncoder().encode(page.text).length > MaxPageBytes
  )
    throw new Error('Invalid log page');
  return page;
}

export function checkList(list: LogList): LogList {
  if (
    !list ||
    typeof list.generation !== 'string' ||
    !Array.isArray(list.files) ||
    list.files.length > MaxCatalogFiles ||
    list.files.some(
      (file) =>
        typeof file?.id !== 'string' ||
        !Number.isSafeInteger(file.size) ||
        file.size < 0
    ) ||
    (list.nextOffset !== undefined &&
      (!Number.isSafeInteger(list.nextOffset) || list.nextOffset < 0))
  )
    throw new Error('Invalid log catalog');
  return list;
}

export function settingsConfig(
  enabled: boolean,
  quota: string,
  unit: number,
  age: string,
  info: LogInfo,
  state?: LogState
): LogConfig | null {
  if (!quota.trim() || !age.trim()) return null;
  const preview = quotaPreview(quota, unit, info, state);
  const maxAgeDays = Number(age);
  if (
    !preview ||
    (unit !== PercentUnit && preview.quotaBytes < info.minQuotaBytes) ||
    (enabled && preview.effectiveQuotaBytes < info.minQuotaBytes) ||
    (unit !== PercentUnit && preview.quotaBytes > info.maxQuotaBytes) ||
    !Number.isSafeInteger(maxAgeDays) ||
    maxAgeDays < 0 ||
    maxAgeDays > MaxAgeDays
  )
    return null;
  const { quotaBytes, quotaPercent } = preview;
  return { enabled, quotaBytes, quotaPercent, maxAgeDays };
}

export function trimsHistory(previous: LogConfig, next: LogConfig) {
  return (
    next.quotaBytes < previous.quotaBytes ||
    ((previous.quotaPercent || 0) > 0 &&
      (next.quotaPercent || 0) > 0 &&
      next.quotaPercent! < previous.quotaPercent!) ||
    (next.maxAgeDays > 0 &&
      (previous.maxAgeDays === 0 || next.maxAgeDays < previous.maxAgeDays))
  );
}

export function downloadUrl(
  id?: string,
  host = typeof window === 'undefined'
    ? undefined
    : window.esp32m?.backend?.host,
  origin = typeof window === 'undefined'
    ? 'http://localhost'
    : window.location.origin
) {
  const protocol = new URL(origin).protocol === 'https:' ? 'https:' : 'http:';
  const base = host
    ? /^https?:\/\//.test(host)
      ? host
      : `${protocol}//${host}`
    : origin;
  const url = new URL('/logs/download', base);
  if (id !== undefined) url.searchParams.set('id', id);
  return url.toString();
}
