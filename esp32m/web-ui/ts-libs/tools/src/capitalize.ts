import { isString } from './is-type';

export const capitalize = (s: string | undefined) => {
  if (!isString(s) || s.length == 0) return s;
  if (s.length == 1) return s.toUpperCase();
  return `${s.charAt(0).toUpperCase()}${s.substring(1).toLowerCase()}`;
};
