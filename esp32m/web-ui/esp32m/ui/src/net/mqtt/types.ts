export const Name = 'mqtt';

export type TMqttConfig = {
  enabled: boolean;
  uri: string;
  client: string;
  username: string;
  password: string;
  keepalive: number;
  timeout: number;
  cert_url: string;
};

export type TMqttState = {
  ready: boolean;
  uri: string;
  client: string;
  pubcnt: number;
  recvcnt: number;
};
