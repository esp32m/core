export const Name = "ble";

export const enum AdvType {
  Normal = 0,
  Directed,
  Scannable,
  NonConnectable,
  Response,
}

export const enum AdvField {
  Flags = 0x01,
  IncompUuids16 = 0x02,
  CompUuids16 = 0x03,
  IncompUuids32 = 0x04,
  CompUuids32 = 0x05,
  IncompUuids128 = 0x06,
  CompUuids128 = 0x07,
  IncompName = 0x08,
  CompName = 0x09,
  TxPwrLvl = 0x0a,
  SlaveItvlRange = 0x12,
  SolUuids16 = 0x14,
  SolUuids128 = 0x15,
  SvcDataUuid16 = 0x16,
  PublicTgtAddr = 0x17,
  RandomTgtAddr = 0x18,
  Appearance = 0x19,
  AdvItvl = 0x1a,
  SvcDataUuid32 = 0x20,
  SvcDataUuid128 = 0x21,
  Uri = 0x24,
  MeshProv = 0x29,
  MeshMessage = 0x2a,
  MeshBeacon = 0x2b,
  MfgData = 0xff,
}

export const enum AdvDataFlags {
  DiscLtd = 0x01,
  DiscGen = 0x02,
  BredrUnsup = 0x04,
}

export interface IAdvData {
  flags?: AdvDataFlags;
  name?: string;
}

export interface IPeripheral {
  rssi: number;
  addr: string;
  directAddr?: string;
  data?: IAdvData;
}

export interface IAdv extends IPeripheral {
  type: AdvType;
}

export type TPeripherals = {
  [key: string]: IPeripheral;
};
