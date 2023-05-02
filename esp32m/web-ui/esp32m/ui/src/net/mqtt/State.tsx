import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../..';
import { NameValueList } from '../../app';

import { Name, TMqttState } from './types';
import { useTranslation } from '@ts-libs/ui-i18n';

export const MqttStateBox = () => {
  const state = useModuleState<TMqttState>(Name);
  const { t } = useTranslation();
  if (!state) return null;
  const { ready, uri, client, pubcnt, recvcnt } = state;
  const list = [];
  list.push(['Connection state', t(ready ? 'connected' : 'not connected')]);
  if (uri) list.push(['URL', uri]);
  if (client) list.push(['Client name', client]);
  if (pubcnt) list.push(['Published messages', pubcnt]);
  if (recvcnt) list.push(['Received messages', recvcnt]);
  return (
    <CardBox title="MQTT client state">
      <NameValueList list={list} />
    </CardBox>
  );
};
