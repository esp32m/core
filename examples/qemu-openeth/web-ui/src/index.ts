import {
  startUi,
  System,
  NetworkInterfaces,
} from "@esp32m/ui";

startUi({ plugins: [System, NetworkInterfaces] });
