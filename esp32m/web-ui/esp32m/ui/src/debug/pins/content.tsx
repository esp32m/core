import { CardBox } from '@ts-libs/ui-app';
import { useCachedRequest, useRequest } from '../../backend';
import { Name, TPinNames, actions, selectors } from './state';
import {
  Button,
  Chip,
  FormControl,
  Grid,
  InputLabel,
  MenuItem,
  Select,
  Typography,
} from '@mui/material';
import { ReactElement, useEffect, useMemo } from 'react';
import { useDispatch, useSelector } from 'react-redux';
import { enumTool, isNumber } from '@ts-libs/tools';
import { Form } from './form';
import { FeatureStatus, FeatureType, PinFlagBits, TPinState } from './types';
import { useFeature, useFeaturePlugins } from './hooks';

const Flags = () => {
  const pinState = useSelector(selectors.pinState);
  if (!pinState) return null;
  const flags = enumTool(PinFlagBits).bitsToNames(pinState.flags);
  const content = flags.map((f, i) => (
    <Grid key={i} item sx={{ marginLeft: 'auto' }}>
      <Chip label={f || ''} />
    </Grid>
  ));
  if (!content.length) return null;
  return (
    <Grid container spacing={1} item>
      <Grid item xs={12}>
        <Typography align="center">Pin capabilities:</Typography>
      </Grid>
      {content}
    </Grid>
  );
};

const PinSelect = () => {
  const [pinNames] = useCachedRequest<TPinNames | undefined>(Name, 'enum', {
    action: actions.names,
    selector: selectors.names,
  });
  const namesOptions = useMemo(() => {
    if (!pinNames) return [];
    return Object.entries(pinNames).map(([name, pins]) =>
      pins.map((p, i) => (
        <MenuItem value={JSON.stringify([name, p])} key={i}>
          {`${name}-${p}`}
        </MenuItem>
      ))
    );
  }, [pinNames]);
  const pin = useSelector(selectors.pin);
  const dispatch = useDispatch();
  return (
    <FormControl variant="standard" fullWidth>
      <InputLabel>I/O pin</InputLabel>
      <Select
        value={pin ? JSON.stringify(pin) : ''}
        onChange={(e) => {
          dispatch(
            actions.pin(e.target.value ? JSON.parse(e.target.value) : undefined)
          );
        }}
      >
        {namesOptions}
      </Select>
    </FormControl>
  );
};

const FeatureSelect = () => {
  const pinState = useSelector(selectors.pinState);
  const feature = useSelector(selectors.feature);
  const { features } = pinState || {};
  const plugins = useFeaturePlugins();
  const featuresOptions = useMemo(() => {
    if (!features) return [];
    return features.reduce((a, s, i) => {
      const f = plugins[i as FeatureType];
      if (s != FeatureStatus.NotSupported && f) {
        a.push(
          <MenuItem
            value={i}
            key={i}
            sx={{ fontWeight: s == FeatureStatus.Enabled ? 'bold' : 'normal' }}
          >
            {f.name}
          </MenuItem>
        );
      }
      return a;
    }, [] as Array<ReactElement>);
  }, [features, plugins]);
  const dispatch = useDispatch();
  const featureStatus = isNumber(feature) && features?.[feature];
  return (
    <FormControl
      variant="standard"
      fullWidth
      disabled={featuresOptions.length == 0}
    >
      <InputLabel>Feature</InputLabel>
      <Select
        value={featureStatus ? feature : ''}
        onChange={(e) => {
          dispatch(
            actions.feature(
              isNumber(e.target.value) ? e.target.value : undefined
            )
          );
        }}
      >
        {featuresOptions}
      </Select>
    </FormControl>
  );
};

export const content = () => {
  const pin = useSelector(selectors.pin);
  const feature = useSelector(selectors.feature);
  const f = useFeature();
  const [pinState, pinStateRefresh] = useRequest<TPinState>(Name, 'state-get', {
    data: f.stateRequestData,
    condition: !!pin,
  });
  const dispatch = useDispatch();
  useEffect(() => {
    dispatch(actions.pinState(pinState));
  }, [pinState, dispatch]);
  const { features, state } = pinState || {};
  useEffect(() => {
    if (isNumber(feature) && !features?.[feature])
      dispatch(actions.feature(undefined));
  }, [feature, features, dispatch]);
  const [, featureRequest, featureRequestRunning] = useRequest(
    Name,
    'feature',
    { data: [pin, feature], condition: false }
  );
  return (
    <CardBox title="Pins">
      <Grid container spacing={2}>
        <Grid container spacing={2} item xs={pin ? 4 : 12}>
          <Grid item xs={12}>
            <PinSelect />
          </Grid>
          <Grid item xs={12}>
            {pin && <FeatureSelect />}
          </Grid>
        </Grid>
        {pin && (
          <Grid container spacing={2} item xs={8}>
            <Flags />
          </Grid>
        )}

        {f.status == FeatureStatus.Supported && f.info && (
          <Grid item xs>
            <Button
              disabled={featureRequestRunning}
              onClick={() => {
                featureRequest().then(() => pinStateRefresh());
              }}
            >{`Enable ${f.info.name} feature`}</Button>
          </Grid>
        )}
        {f.status == FeatureStatus.Enabled && state && (
          <Form onChange={pinStateRefresh} />
        )}
      </Grid>
    </CardBox>
  );
};
