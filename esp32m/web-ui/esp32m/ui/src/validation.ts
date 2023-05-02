import { callOnce } from '@ts-libs/tools';
import * as Yup from 'yup';

function ipv4(this: Yup.StringSchema, message = 'Invalid IP address') {
  return this.matches(/(^(\d{1,3}\.){3}(\d{1,3})$)/, {
    message,
    excludeEmptyString: true,
  }).test('ip', message, (value) => {
    return value === undefined || value?.trim() === ''
      ? true
      : value?.split('.').find((i) => parseInt(i, 10) > 255) === undefined;
  });
}
function ipv6(this: Yup.StringSchema, message = 'Invalid IP address') {
  return this.matches(
    /^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$/,
    {
      message,
      excludeEmptyString: true,
    }
  );
}
function macaddr(this: Yup.StringSchema, message = 'Invalid MAC address') {
  return this.matches(/^([0-9A-Fa-f]{2}:){5}([0-9A-Fa-f]{2})$/, {
    message,
    excludeEmptyString: true,
  }).test('MAC', message, (value) => {
    return value === undefined || value?.trim() === ''
      ? true
      : value?.split('.').find((i) => parseInt(i, 10) > 255) === undefined;
  });
}
const init = callOnce(() => {
  Yup.addMethod(Yup.string, 'ipv4', ipv4);
  Yup.addMethod(Yup.string, 'ipv6', ipv6);
  Yup.addMethod(Yup.string, 'macaddr', macaddr);
});

export const useYup = <T>(fn: (y: typeof Yup) => T) => {
  init();
  return fn(Yup);
};

declare module 'yup' {
  interface StringSchema {
    macaddr: () => this;
    ipv4: () => this;
    ipv6: () => this;
  }
}

export const validators = {
  pin: Yup.number().integer().min(1).max(50),
  i2cId: Yup.number().integer().min(1).max(127),
  modbusAddr: Yup.number().integer().min(1).max(247),
};
