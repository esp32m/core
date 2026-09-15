import { CardBox } from '@ts-libs/ui-app';
import {
  Accordion,
  AccordionDetails,
  AccordionSummary,
  Typography,
} from '@mui/material';
import { ExpandMore } from '@mui/icons-material';
import { NameValueList } from '../../app';
import { useModuleState } from '../..';
import { Name, TProps, TState } from './types';

const UnitNames = [
  '°C',
  '°F',
  'MPa',
  'kPa',
  'Pa',
  'bar',
  'mbar',
  'psi',
  'mH₂O',
  'mmH₂O',
];

const RegisterNames = [
  'Address',
  'Baud code',
  'Unit code',
  'Decimals',
  'Measurement',
  'Range zero',
  'Range full',
  'Zero offset',
];

const formatRegister = (value: number, signed: boolean) => {
  const unsigned = value & 0xffff;
  const hex = `0x${unsigned.toString(16).toUpperCase().padStart(4, '0')}`;
  if (signed && unsigned >= 0x8000)
    return `${unsigned - 0x10000} signed / ${unsigned} (${hex})`;
  return `${unsigned} (${hex})`;
};

const formatCalibrationValue = (value: number) =>
  Number(value.toPrecision(10)).toString();

const ProtocolDiagnostics = ({ registers }: { registers: number[] }) => {
  if (registers.length < RegisterNames.length) return null;
  const list: Array<[string, string]> = RegisterNames.map((name, index) => [
    `R${index} ${name}`,
    formatRegister(registers[index], index >= 4),
  ]);
  return (
    <Accordion>
      <AccordionSummary
        expandIcon={<ExpandMore />}
        id="maqi136-protocol-diagnostics-header"
      >
        <Typography>Protocol diagnostics</Typography>
      </AccordionSummary>
      <AccordionDetails>
        <NameValueList list={list} />
      </AccordionDetails>
    </Accordion>
  );
};

export const Content = ({ title = Name, addr }: TProps) => {
  const state = useModuleState<TState>(Name);
  if (!state) return null;
  const [
    age = -1,
    actualAddr,
    unit = 0,
    decimals = 0,
    precision = 3,
    min = 0,
    max = 0,
    value = 0,
    offset = 0,
    registers = [],
    calibrated = false,
    calibrationMultiplier = 0,
    calibrationOffset = 0,
  ] = state;
  if (age < 0 || age > 60 * 1000) return null;
  if (addr && addr != actualAddr) return null;

  const nativeUnit = UnitNames[unit] || `Unknown (${unit})`;
  const underRange = calibrated && registers.length > 4 && registers[4] === 0;
  const list: Array<[string, string | number]> = [
    [
      'Level',
      underRange
        ? `${value.toFixed(precision)} m (under-range)`
        : `${value.toFixed(precision)} m`,
    ],
    ['Range', `${min.toFixed(precision)}–${max.toFixed(precision)} m`],
    ['Native format', `${nativeUnit}, ${decimals} decimals`],
  ];
  if (calibrated) {
    const sign = calibrationOffset < 0 ? '−' : '+';
    list.push([
      'Calibration',
      `${formatCalibrationValue(calibrationMultiplier)} × R4 ${sign} ${formatCalibrationValue(Math.abs(calibrationOffset))} m`,
    ]);
  }
  if (offset) list.push(['Zero offset', `${offset.toFixed(precision)} m`]);
  if (!addr && actualAddr) list.push(['MODBUS address', actualAddr]);

  return (
    <CardBox title={title}>
      <NameValueList list={list} />
      <ProtocolDiagnostics registers={registers} />
    </CardBox>
  );
};
