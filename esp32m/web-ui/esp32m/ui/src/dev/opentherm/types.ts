export enum Status {
  SlaveFault = 1 << 0,
  SlaveCH = 1 << 1,
  SlaveDHW = 1 << 2,
  SlaveFlame = 1 << 3,
  SlaveCooling = 1 << 4,
  SlaveCH2 = 1 << 5,
  SlaveDiag = 1 << 6,
  MasterCH = 1 << 8,
  MasterDHW = 1 << 9,
  MasterCooling = 1 << 10,
  MasterOTC = 1 << 11,
  MasterCH2 = 1 << 12,
}

export type TBounds = {
  dhw?: Array<number>;
  ch?: Array<number>;
  hcr?: Array<number>;
}

export type THvacState = {
  status: number;
  ts: number;
  cfg?: number;
  fault?: number;
  rbp?: number;
  cc?: number;
  tsch2?: number;
  trov?: number;
  tsps?: Array<number>;
  fhb?: Array<number>;
  maxrm?: number;
  maxcap?: number;
  minmod?: number;
  trs?: number;
  rm?: number;
  chp?: number;
  dhwf?: number;
  trsch2?: number;
  tr?: number;
  tb?: number;
  tdhw?: number;
  to?: number;
  tret?: number;
  tst?: number;
  tc?: number;
  tfch2?: number;
  tdhw2?: number;
  tex?: number;
  bfc?: number;
  bounds?: TBounds;
  tdhws?: number;
  maxts?: number;
  hcr?: number;
  rof?: number;
  diag?: number;
  otvm?: number;
  otvs?: number;
  vm?: number;
  vs?: number;
}

export type TMasterState = {
  hvac: THvacState;
}

export type TSlaveState = {
  hvac: THvacState;
}

export type TProps = {
  name: string;
  title?: string;
}
