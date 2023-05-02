export const Name = 'INA219';

export interface IProps {
  title?: string;
  addr?: number;
}

export interface IState {
  addr: number;
  voltage: number;
  shuntVoltage: number;
  current: number;
  power: number;
}
