import React from 'react';
import { rssiToStr, rssiToLevel } from '../utils';
import {
  SignalCellular0Bar,
  SignalCellular1Bar,
  SignalCellular2Bar,
  SignalCellular3Bar,
  SignalCellular4Bar,
} from '@mui/icons-material';
import { Tooltip } from '@mui/material';

interface IProps {
  rssi: number;
}

const icons = [
  SignalCellular0Bar,
  SignalCellular1Bar,
  SignalCellular2Bar,
  SignalCellular3Bar,
  SignalCellular4Bar,
];

export default function ({ rssi }: IProps) {
  const I: React.ComponentType = icons[rssiToLevel(rssi, 5)];
  const tt = (
    <>
      <div>{'signal: ' + rssiToStr(rssi)}</div>
    </>
  );
  return (
    <Tooltip title={tt}>
      <I />
    </Tooltip>
  );
}
