import { IDevicePlugin } from "../types";
import { Devices } from "../shared";
import { Name } from './types';
import { Content } from "./content";

export const Stm32Act: IDevicePlugin = {
    name: Name,
    use: Devices,
    device: { component: Content },
};
