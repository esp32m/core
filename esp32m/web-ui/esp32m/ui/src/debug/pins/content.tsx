import { CardBox } from '@ts-libs/ui-app';
import { useCachedRequest, useModuleState, useRequest } from '../../backend';
import { Name, TPinNames, actions, selectors } from './state';
import {
  Button,
  FormControl,
  Grid,
  InputLabel,
  MenuItem,
  Select,
} from '@mui/material';
import { ComponentType, ReactElement, useEffect, useMemo } from 'react';
import { useDispatch, useSelector } from 'react-redux';
import { isNumber } from '@ts-libs/tools';
import { PWM } from './pwm';
import { Form } from './form';
import { Digital } from './digital';

const FeatureNames = [
  'Digital',
  'ADC',
  'DAC',
  'PWM',
  'Pulse counter',
  'LED control',
];

const FeatureComponents: Array<ComponentType | null> = [
  Digital,
  null,
  null,
  PWM,
];

const enum FeatureStatus {
  NotSupported,
  Supported,
  Enabled,
}

type TPinState = {
  features: Array<FeatureStatus>;
  state: any;
};

export const content = () => {
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
  const feature = useSelector(selectors.feature);
  const stateRequestData = useMemo(() => ({ pin, feature }), [pin, feature]);
  const [pinState, pinStateRefresh] = useRequest<TPinState>(Name, 'state-get', {
    data: stateRequestData,
    condition: !!pin,
  });
  const { features, state } = pinState || {};
  const featuresOptions = useMemo(() => {
    if (!features) return [];
    return features.reduce((a, s, i) => {
      if (s != FeatureStatus.NotSupported) {
        a.push(
          <MenuItem
            value={i}
            key={i}
            sx={{ fontWeight: s == FeatureStatus.Enabled ? 'bold' : 'normal' }}
          >
            {FeatureNames[i]}
          </MenuItem>
        );
      }
      return a;
    }, [] as Array<ReactElement>);
  }, [features]);
  const dispatch = useDispatch();
  useEffect(() => {
    if (isNumber(feature) && !features?.[feature])
      dispatch(actions.feature(undefined));
  }, [feature, features, dispatch]);
  const [featureResponse, featureRequest, featureRequestRunning] = useRequest(
    Name,
    'feature',
    { data: [pin, feature], condition: false }
  );
  const featureStatus = isNumber(feature) && features?.[feature];
  const FC = FeatureComponents[feature || 0];
  return (
    <CardBox title="Pins">
      <Grid container spacing={2}>
        <Grid item xs={6}>
          <FormControl variant="standard" fullWidth>
            <InputLabel>I/O pin</InputLabel>
            <Select
              value={pin ? JSON.stringify(pin) : ''}
              onChange={(e) => {
                dispatch(
                  actions.pin(
                    e.target.value ? JSON.parse(e.target.value) : undefined
                  )
                );
              }}
            >
              {namesOptions}
            </Select>
          </FormControl>
        </Grid>
        <Grid item xs={6}>
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
        </Grid>
        {featureStatus == FeatureStatus.Supported && (
          <Grid item xs>
            <Button
              disabled={featureRequestRunning}
              sx={{ float: 'right', clear: 'right' }}
              onClick={() => {
                featureRequest().then(() => pinStateRefresh());
              }}
            >{`Enable ${FeatureNames[feature || 0]} feature`}</Button>
          </Grid>
        )}
        {featureStatus == FeatureStatus.Enabled && FC && state && (
          <Form state={state} onChange={pinStateRefresh}>
            <FC />
          </Form>
        )}
      </Grid>
    </CardBox>
  );
};
