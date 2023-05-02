import { PermScanWifiTwoTone } from '@mui/icons-material';
import { Avatar } from '@mui/material';

import { Name, TWifiState, WifiFlags, ScanEntries } from './types';
import { ScanList } from './ScanList';
import { CardBox } from '@ts-libs/ui-app';
import { useModuleState, useRequest } from '../../backend';

const useScan = () => {
  const [scan, , running] = useRequest<ScanEntries>(Name, 'scan', {
    delay: 1000,
    interval: 30000,
  });
  return [scan, running] as const;
};

export const ScannerBox = () => {
  const state = useModuleState<TWifiState>(Name);
  const [scan, running] = useScan();
  const { flags = 0 } = state || {};
  const scanning = running || !scan || (flags & WifiFlags.Scanning) != 0;
  return (
    <CardBox
      progress={scanning}
      title="Available access points"
      avatar={
        <Avatar>
          <PermScanWifiTwoTone />
        </Avatar>
      }
    >
      {scan && <ScanList scan={scan} />}
    </CardBox>
  );
};
