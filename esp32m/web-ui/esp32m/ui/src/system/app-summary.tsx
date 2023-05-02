import { CardBox } from '@ts-libs/ui-app';
import { useModuleState } from '../backend';
import { formatBytes } from '../utils';
import { IAppState } from './types';
import { NameValueList } from '../app';
import { buildInfo } from '@ts-libs/tools';

const Name = 'app';

function sdkver(version: string | number) {
  if (typeof version !== 'number') return version;
  return `${(version >> 16) & 0xff}.${(version >> 8) & 0xff}.${version & 0xff}`;
}

export const AppSummary = () => {
  const state = useModuleState<IAppState>(Name);
  const { name, built, version, sdk, size } = state || {};
  const list = [];
  if (name) list.push(['Application name', name]);
  if (version || built)
    list.push(['Firmware version / build time', `${version} / ${built}`]);
  if (buildInfo)
    list.push([
      'UI version / build time',
      `${buildInfo.version} / ${buildInfo.built}`,
    ]);
  if (sdk) list.push(['SDK version', sdkver(sdk)]);
  if (size) list.push(['Firmware size', formatBytes(size)]);
  if (!list.length) return null;

  return (
    <CardBox title="Application summary">
      <NameValueList list={list} />
    </CardBox>
  );
};
