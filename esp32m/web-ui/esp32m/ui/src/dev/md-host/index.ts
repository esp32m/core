import { TReduxPlugin } from "@ts-libs/redux";
import { IDevicePlugin } from "../types";
import { Devices } from "../shared";
import { Name } from './types';
import { reducer } from "./state";
import { Content } from "./content";

export const MdHost: IDevicePlugin & TReduxPlugin = {
    name: Name,
    use: Devices,
    redux: { reducer },
    device: { component: Content },
};
