import { MenuItem } from '@mui/material';
import { LedcMode } from './types';

export const toMenuItem = (e: Array<any>, i: number) => (
  <MenuItem key={i} value={e[0]}>
    {e[1]}
  </MenuItem>
);
export const LedcTimers = Array.from(Array(4).keys())
  .map((v) => [v, 'Timer #' + v])
  .map(toMenuItem);

export const LedcModes = [
  [LedcMode.HighSpeed, 'High speed'],
  [LedcMode.LowSpeed, 'Low speed'],
];
