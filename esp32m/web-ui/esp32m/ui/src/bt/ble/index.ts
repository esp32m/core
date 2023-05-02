import { Bluetooth, IBluetoothPlugin } from "../shared";

import Content from "./Content";
import { Name } from "./types";
import { TPlugin } from "@ts-libs/plugins";

export const Ble: IBluetoothPlugin & TPlugin = {
  name: Name,
  use: Bluetooth,
  bt: { content: Content },
};
