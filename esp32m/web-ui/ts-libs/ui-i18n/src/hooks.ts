import { isString } from '@ts-libs/tools';
import { ReactNode, useCallback } from 'react';

import { useTranslation as useRi18nTranslation } from 'react-i18next';

export const useTranslation = () => {
  const ret = useRi18nTranslation();
  const { t } = ret;
  const tr = useCallback(
    (el: string | ReactNode) => {
      if (isString(el)) return t(el);
      return el;
    },
    [t]
  );
  return { ...ret, tr };
};

export const useTimeTranslation = () => {
  const { t, i18n } = useTranslation();

  function plural(number: number) {
    return number > 1 ? 's' : '';
  }

  function years(v: number) {
    const rem10 = v % 10;
    switch (i18n.language) {
      case 'uk':
        if (v == 1 || (rem10 == 1 && v > 20)) return `${v} рік`;
        if ([2, 3, 4].includes(rem10) && (v > 20 || v < 10)) return `${v} роки`;
        return `${v} років`;
      case 'de':
        if (v == 1) return `${v} Jarh`;
        return `${v} Jarhe`;
      default:
        return `${v} year${plural(v)}`;
    }
  }
  function days(v: number) {
    const rem10 = v % 10;
    switch (i18n.language) {
      case 'uk':
        if (v == 1 || (rem10 == 1 && v > 20)) return `${v} день`;
        if ([2, 3, 4].includes(rem10) && (v > 20 || v < 10)) return `${v} дні`;
        return `${v} днів`;
      case 'de':
        if (v == 1) return `${v} Tag`;
        return `${v} Tage`;
      default:
        return `${v} day${plural(v)}`;
    }
  }
  function hours(v: number) {
    const rem10 = v % 10;
    switch (i18n.language) {
      case 'uk':
        if (v == 1 || (rem10 == 1 && v > 20)) return `${v} година`;
        if ([2, 3, 4].includes(rem10) && (v > 20 || v < 10))
          return `${v} години`;
        return `${v} годин`;
      case 'de':
        if (v == 1) return `${v} Stunde`;
        return `${v} Stunden`;
      default:
        return `${v} hour${plural(v)}`;
    }
  }
  function minutes(v: number) {
    const rem10 = v % 10;
    switch (i18n.language) {
      case 'uk':
        if (v == 1 || (rem10 == 1 && v > 20)) return `${v} хвилина`;
        if ([2, 3, 4].includes(rem10) && (v > 20 || v < 10))
          return `${v} хвилини`;
        return `${v} хвилин`;
      case 'de':
        if (v == 1) return `${v} Minute`;
        return `${v} Minuten`;
      default:
        return `${v} minute${plural(v)}`;
    }
  }
  function seconds(v: number) {
    const rem10 = v % 10;
    switch (i18n.language) {
      case 'uk':
        if (v == 1 || (rem10 == 1 && v > 20)) return `${v} секунда`;
        if ([2, 3, 4].includes(rem10) && (v > 20 || v < 10))
          return `${v} секунди`;
        return `${v} секунд`;
      case 'de':
        if (v == 1) return `${v} Sekunde`;
        return `${v} Sekunden`;
      default:
        return `${v} second${plural(v)}`;
    }
  }
  const elapsed = (milliseconds: number) => {
    let temp = Math.floor(milliseconds / 1000);
    const y = Math.floor(temp / 31536000);
    if (y) return years(y);

    const d = Math.floor((temp %= 31536000) / 86400);
    if (d) return days(d);

    const h = Math.floor((temp %= 86400) / 3600);
    if (h) return hours(h);

    const m = Math.floor((temp %= 3600) / 60);
    if (m) return minutes(m);

    const s = temp % 60;
    if (s) return seconds(s);

    return t('less than a second');
  };
  return { elapsed };
};
