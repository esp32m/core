import { useMemo } from 'react';
import {
  FeatureType,
  TDebugPinFeaturePlugin,
  TFeatureInfo,
  TPinState,
} from './types';
import { getPlugins } from '@ts-libs/plugins';
import { Name, selectors } from './state';
import { useSelector } from 'react-redux';
import { isNumber } from '@ts-libs/tools';
import { useModuleState } from '../../backend';
import { TJsonValue } from '../../../../../ts-libs/tools/src';

type TFeatureComponents = { [key in FeatureType]: TFeatureInfo };

export function useFeaturePlugins() {
  const plugins = getPlugins<TDebugPinFeaturePlugin>();
  return useMemo(
    () =>
      plugins
        .filter((p) => p.debug?.pin?.features)
        .flatMap((p) => p.debug.pin.features)
        .reduce((m, f) => {
          m[f.feature] = f;
          return m;
        }, {} as TFeatureComponents),
    [plugins]
  );
}

export function useFeature() {
  const plugins = useFeaturePlugins();
  const pin = useSelector(selectors.pin);
  const feature = useSelector(selectors.feature);
  const stateRequestData = useMemo(() => ({ pin, feature }), [pin, feature]);
  const { features } = useSelector(selectors.pinState) || {};
  if (isNumber(feature) && features)
    return {
      pin,
      feature,
      stateRequestData,
      status: features[feature],
      info: plugins[feature],
    };
  return { pin, stateRequestData };
}

export function useFeatureState<T extends TJsonValue>() {
  const feature = useFeature();
  return useModuleState<TPinState<T>>(Name, {
    data: feature.stateRequestData,
  });
}
