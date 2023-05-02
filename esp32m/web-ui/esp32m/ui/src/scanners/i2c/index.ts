import { Reducer } from "redux";
import { Name } from "./types";
import { Scanners } from "../shared";
import Scanner from "./Scanner";
import { IScannerPlugin } from "../types";
import { TReduxPlugin } from "@ts-libs/redux";

const reducer: Reducer = (state = {}, { type, payload }) => {
  if (payload && payload.source == Name && payload.name == "scan")
    switch (type) {
      case "backend/response":
        return { ...state, scan: payload.data };
      case "backend/request":
        return { ...state, scan: {} };
    }
  return state;
};

export const I2CScanner: IScannerPlugin & TReduxPlugin = {
  name: Name,
  use: Scanners,
  redux: { reducer },
  scanner: { component: Scanner },
};
