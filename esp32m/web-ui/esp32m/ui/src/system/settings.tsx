import { Grid, MenuItem } from '@mui/material';

import { Name, ISystemConfig } from './types';
import { FieldSelect, FieldSwitch } from '@ts-libs/ui-forms';
import { useModuleConfig } from '../backend';
import { ConfigBox } from '../app';
import { Expander } from '@ts-libs/ui-base';

const FreqOptions = [
  [undefined, 'Default'],
  [240, '240 MHz'],
  [160, '160 MHz'],
  [80, '80 MHz'],
];

const FreqField = ({ name }: { name: string }) => (
  <FieldSelect
    fullWidth
    name={name}
    label={(name == 'pm.fmin' ? 'Min' : 'Max') + '. CPU frequency'}
  >
    {FreqOptions.map((o) => (
      <MenuItem key={o[0] || '_'} value={o[0]}>
        {o[1]}
      </MenuItem>
    ))}
  </FieldSelect>
);

export const Settings = () => {
  const [config] = useModuleConfig<ISystemConfig>(Name);
  if (!config || !config.pm) return null;

  return (
    <ConfigBox name={Name} initial={config} title="System settings">
      <Expander title="Power management" defaultExpanded>
        <Grid item xs>
          <FieldSwitch
            name="pm.lse"
            label="Enable Dynamic Frequency Scaling (light sleep)"
          />
        </Grid>
        <Grid container spacing={3}>
          <Grid item xs>
            <FreqField name="pm.fmin" />
          </Grid>
          <Grid item xs>
            <FreqField name="pm.fmax" />
          </Grid>
        </Grid>
      </Expander>
    </ConfigBox>
  );
};
