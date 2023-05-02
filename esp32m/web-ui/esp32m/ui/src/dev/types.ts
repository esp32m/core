import { TPlugin } from "@ts-libs/plugins";
import React from "react";

export type IDevicePlugin<P extends Record<string, unknown> = any> = TPlugin & {
  device?: {
    component: React.ComponentType<P>;
    props?: P;
  };
};
