import { TReduxPlugin } from '@ts-libs/redux';
import { Debug } from '../shared';
import { TDebugPlugin } from '../types';
import { content } from './content';
import { Name, reducer } from './state';
import { DigitalPlugin } from './digital';
import { AdcPlugin } from './adc';
import { PwmPlugin } from './pwm';
import { DacPlugin } from './dac';

export const DebugPins = (): TDebugPlugin & TReduxPlugin => ({
  name: Name,
  use: [Debug, DigitalPlugin, AdcPlugin, DacPlugin, PwmPlugin],
  redux: { reducer },
  debug: { content },
});
