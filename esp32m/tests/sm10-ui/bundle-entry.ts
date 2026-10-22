import { Sm10 } from "../../web-ui/esp32m/ui/src/dev/sm10";

// Export the plugin so production tree shaking must retain its real component.
export const sm10 = Sm10({ name: "SM10" });
