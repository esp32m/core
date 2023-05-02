import { useCallback, useEffect, useState } from "react";

import * as Backend from "../../backend";

import {
  Name,
  IAdvData,
  AdvField,
  AdvDataFlags,
  IAdv,
  AdvType,
  TPeripherals,
} from "./types";
import { Bluetooth } from "@mui/icons-material";
import {
  Avatar,
  List,
  ListItem,
  ListItemIcon,
  ListItemText,
} from "@mui/material";
import RssiIcon from "../RssiIcon";
import { CardBox } from "@ts-libs/ui-app";
import { useBackendApi } from "../../backend";
import { fromBase64 } from "../../base64";

function parseData(buf: ArrayBuffer): IAdvData | undefined {
  const bytes = new Uint8Array(buf);
  const byteLength = bytes.byteLength;
  let ofs = 0;
  const r: IAdvData = {};
  while (ofs < byteLength) {
    const len = bytes[ofs++] - 1; // length without type octet
    const type = bytes[ofs++] as AdvField;
    switch (type) {
      case AdvField.Flags:
        if (len != 1) return;
        r.flags = bytes[ofs] as AdvDataFlags;
        break;
      case AdvField.CompName:
      case AdvField.IncompName:
        {
          let n = "";
          for (let i = 0; i < len; i++)
            n += String.fromCharCode(bytes[ofs + i]);
          r.name = n;
        }
        break;
    }
    ofs += len;
  }
  return r;
}

function toAdv(e: Array<unknown>): IAdv {
  const result: IAdv = {
    type: e[0] as AdvType,
    rssi: e[1] as number,
    addr: e[2] as string,
  };
  if (e[3]) result.directAddr = e[3] as string;
  if (e[4]) result.data = parseData(fromBase64(e[4] as string));
  return result;
}

function toPeripherals(payload: any) {
  const peripherals: TPeripherals = {};
  const advs = (payload.data as Array<[]>).map(toAdv);
  for (const a of advs) {
    const e = peripherals[a.addr];
    if (!e || a.data) peripherals[a.addr] = a;
  }
  return peripherals;
}

export default () => {
  const api = useBackendApi();
  const [peripherals, setPeripherals] = useState<TPeripherals>({});
  const cancel = useCallback(() => api?.request(Name, "cancel"), [api]);
  const request = useCallback(
    async (name: string, data?: any) => {
      let response: Backend.TResponse | undefined;
      try {
        response = await api?.request(Name, name, data);
      } catch (e) {
        if (e == 259) {
          await cancel();
          response = await api?.request(Name, name, data);
        }
      }
      if (response) setPeripherals(toPeripherals(response.data));
    },
    [api, cancel]
  );
  useEffect(() => {
    request("scan", { active: true });
    return () => {
      cancel();
    };
  }, []);
  return (
    <CardBox
      title="BLE peripherals"
      avatar={
        <Avatar>
          <Bluetooth />
        </Avatar>
      }
    >
      {peripherals && (
        <List>
          {Object.values(peripherals).map((p, i) => {
            const { data, addr, rssi } = p;
            const { name } = data || {};
            return (
              <ListItem key={"row" + i}>
                <ListItemText
                  primary={name || addr}
                  secondary={!name && addr}
                />
                <ListItemIcon>
                  <RssiIcon {...{ rssi }} />
                </ListItemIcon>
              </ListItem>
            );
          })}
        </List>
      )}
    </CardBox>
  );
};
