import {
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
} from '@mui/material';
import { NameValueList, useSampleAge } from '../..';
import type { CdhxState } from './types';
import { age, value } from './format';

type Props = {
  name: string;
  state: CdhxState;
  onClose: () => void;
};

export default ({ name, state, onClose }: Props) => {
  const { display } = state;
  const displayAge = useSampleAge(display.sampledAtMs > 0 ? display.sampledAtMs : -1, name);
  const titleId = `${name}-display-title`;

  return (
    <Dialog
      open
      fullWidth
      maxWidth="sm"
      onClose={onClose}
      aria-labelledby={titleId}
    >
      <DialogTitle id={titleId}>Physical display (read-only)</DialogTitle>
      <DialogContent>
        <NameValueList
          list={[
            ['Mode', display.mode],
            [
              'Ambient temperature',
              display.mode === 'temperature'
                ? value(display.ambientC, 0, '°C')
                : 'Unavailable in this mode',
            ],
            [
              'Requested temperature',
              value(display.temperatureSetpointC, 0, '°C'),
            ],
            ['Display requested setting (raw)', value(display.requestedRaw)],
            ['Request age', age(display.valid && displayAge >= 0, displayAge)],
          ]}
        />
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>Close</Button>
      </DialogActions>
    </Dialog>
  );
};
