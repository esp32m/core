import * as Yup from 'yup';
import {
  Autocomplete,
  Box,
  Button,
  Grid,
  TextField,
  TextFieldProps,
} from '@mui/material';

import { Name, IConfig, IState } from './types';

import { useEffect, useMemo, useState } from 'react';
import { getIn, useField, useFormikContext } from 'formik';
import zones from './zones.json';
import { useBackendApi, useModuleConfig, useModuleState } from '../../backend';
import { FieldSwitch, FieldText } from '@ts-libs/ui-forms';
import { ConfigBox, NameValueList } from '../../app';
import { useTimeTranslation, useTranslation } from '@ts-libs/ui-i18n';

const ValidationSchema = Yup.object().shape({});

// const StatusNames = ['reset', 'complete', 'in progress'];
const TzrSelect = () => {
  const name = 'tzr';
  const options = useMemo(() => Object.keys(zones), []);
  const controller = useFormikContext<any>();
  const { t } = useTranslation();
  const {
    errors,
    touched,
    handleBlur: onBlur,
    setFieldValue,
    setFieldTouched,
  } = controller;

  const [field] = useField(name);
  const { value = null } = field;
  const meTouched = getIn(touched, name);

  useEffect(() => {
    // set the value of textC, based on textA and textB
    if (meTouched) {
      /*let ofs = 0,
        dst = 0;
      const zone = value ? moment.tz.zone(value) : undefined;
      if (zone) {
        const year = new Date().getFullYear();
        ofs = -zone.utcOffset(new Date(year, 11, 22).valueOf());
        dst = -zone.utcOffset(new Date(year, 5, 21).valueOf());
      }
      setFieldValue("ofs", ofs);
      setFieldValue("dst", dst);*/
      setFieldValue('tze', (zones as any)[value] || null);
    }
  }, [value, meTouched, setFieldValue]);

  const helperText = getIn(errors, name) && meTouched;
  const tp: TextFieldProps = {
    error: !!helperText,
    name,
    onBlur,
    helperText,
    label: t('Time zone'),
    variant: 'standard',
    fullWidth: true,
  };
  const autocompleteProps = {
    value,
    // defaultValue: moment.tz.guess(),
    onChange: (e: any, value: any) => {
      setFieldValue(name, value);
      setFieldTouched(name, true);
    },
    options,
  };
  return (
    <Grid item container xs={12} spacing={1}>
      <Grid item xs>
        <Autocomplete
          disablePortal
          {...autocompleteProps}
          renderInput={(params: TextFieldProps) => (
            <TextField {...params} {...tp} />
          )}
        />
      </Grid>
      <Grid item style={{ alignSelf: 'end' }}>
        <Button
          variant="outlined"
          onClick={() => {
            const guessed = Intl.DateTimeFormat().resolvedOptions().timeZone;
            // moment.tz.guess();
            if (guessed) {
              setFieldValue(name, guessed);
              controller.setFieldTouched(name, true);
            }
          }}
        >
          {t('Autodetect')}
        </Button>
      </Grid>
    </Grid>
  );
};

const TzManual = () => {
  return <></>;
};
/*
      <Grid item xs={2}>
        <FieldTzofs name="ofs" label="TZ offset" fullWidth disabled/>
      </Grid>
      <Grid item xs={2}>
        <FieldTzofs name="dst" label="DST offset" fullWidth disabled/>
      </Grid>

*/
const Config = () => {
  const api = useBackendApi();
  const [requestInProgress, setRequestInProgress] = useState(false);
  const { t } = useTranslation();
  const requestSync = async () => {
    setRequestInProgress(true);
    try {
      await api.request(Name, 'sync');
    } finally {
      setRequestInProgress(false);
    }
  };
  const controller = useFormikContext<any>();
  const {
    values: { enabled },
  } = controller;
  return (
    <Box sx={{ marginTop: 2 }}>
      <Grid container columnSpacing={2} rowSpacing={1}>
        <Grid item xs>
          <FieldSwitch name="enabled" label="Enable time synchronization" />
        </Grid>
        {enabled && (
          <>
            <Grid>
              <Button
                variant="outlined"
                onClick={requestSync}
                disabled={requestInProgress}
              >
                {t('Synchronize now')}
              </Button>
            </Grid>
            <TzrSelect />
            <TzManual />

            <Grid item xs={7}>
              <FieldText name="tze" label="Time zone setting" fullWidth />
            </Grid>
            <Grid item xs={3}>
              <FieldText name="host" label="NTP hostname" fullWidth />
            </Grid>
            <Grid item xs={2}>
              <FieldText name="interval" label="Interval, s" fullWidth />
            </Grid>
          </>
        )}
      </Grid>
    </Box>
  );
};

const Settings = () => {
  const state = useModuleState<IState>(Name);
  const [config, refresh] = useModuleConfig<IConfig>(Name);
  const { elapsed } = useTimeTranslation();
  const { t } = useTranslation();
  if (!config || !state) return null;
  const { time, synced } = state;
  const list = [];
  list.push([
    'Last synchronization',
    synced ? `${elapsed(synced)} ${t('ago')}` : t('never happened'),
  ]);
  if (time) list.push(['Current on-chip local time', time]);
  return (
    <ConfigBox
      name={Name}
      initial={config}
      onChange={refresh}
      title="SNTP Time Synchronization"
      validationSchema={ValidationSchema}
    >
      <NameValueList list={list} />
      <Config />
    </ConfigBox>
  );
};

export const Content = () => {
  return (
    <Grid container>
      <Settings />
    </Grid>
  );
};
