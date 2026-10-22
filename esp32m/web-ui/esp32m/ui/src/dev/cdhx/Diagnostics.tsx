import {
  Box,
  Button,
  Dialog,
  DialogActions,
  DialogContent,
  DialogTitle,
  Divider,
  Stack,
  Typography,
} from '@mui/material';
import { NameValueList, useSampleAge } from '../..';
import type { CdhxState } from './types';
import { age, byte, namedCode, value } from './format';

type Props = {
  name: string;
  state: CdhxState;
  onClose: () => void;
};

export default ({ name, state, onClose }: Props) => {
  const { control, display, heater, serial } = state;
  const heaterAge = useSampleAge(heater.sampledAtMs > 0 ? heater.sampledAtMs : -1, name);
  const faultDetail = serial.faultDetail;
  const titleId = `${name}-diagnostics-title`;

  return (
    <Dialog
      open
      fullWidth
      maxWidth="sm"
      onClose={onClose}
      aria-labelledby={titleId}
    >
      <DialogTitle id={titleId}>Diagnostics</DialogTitle>
      <DialogContent>
        <Stack spacing={2}>
          <NameValueList
            list={[
              ['Pending command', control.pendingCommand || 'None'],
              ['Last command', control.lastCommand || 'None'],
              ['Command result', control.lastCommandResult || '—'],
              ['Response age', age(heater.valid && heaterAge >= 0, heaterAge)],
              ['Heater status', namedCode(heater.status, heater.statusName)],
              [
                'Heater setting feedback (raw)',
                value(heater.settingFeedbackRaw),
              ],
            ]}
          />
          <Typography variant="caption" color="text.secondary">
            Setting feedback is the heater’s internal temperature setpoint in
            temperature mode, or a calculated pump setting in power mode. It
            does not measure delivered heat.
          </Typography>
          <Divider />
          <Box>
            <Typography variant="subtitle2">Serial diagnostics</Typography>
            <NameValueList
              list={[
                [
                  'Received display / heater bytes',
                  `${serial.rxDisplay} / ${serial.rxHeater}`,
                ],
                [
                  'Forwarded requests / responses',
                  `${serial.forwardedRequests} / ${serial.forwardedResponses}`,
                ],
                [
                  'CRC / framing / echo errors',
                  `${serial.crcErrors} / ${serial.framingErrors} / ${serial.echoErrors}`,
                ],
                ['Response timeouts', String(serial.timeouts)],
                ['Last error', serial.lastError || 'None'],
                ...(serial.workerMaxGapUs !== undefined
                  ? [
                      [
                        'Maximum worker gap',
                        value(serial.workerMaxGapUs, 0, 'μs'),
                      ],
                    ]
                  : []),
                ...(serial.workerMaxStepUs !== undefined
                  ? [
                      [
                        'Maximum worker step',
                        value(serial.workerMaxStepUs, 0, 'μs'),
                      ],
                    ]
                  : []),
                ...(serial.rtosTickUs !== undefined
                  ? [['RTOS tick', value(serial.rtosTickUs, 0, 'μs')]]
                  : []),
                ...(serial.echoDelayedReads !== undefined
                  ? [['Delayed echo reads', value(serial.echoDelayedReads)]]
                  : []),
                ...(serial.echoMaxServiceDelayUs !== undefined
                  ? [
                      [
                        'Maximum echo service delay (µs)',
                        value(serial.echoMaxServiceDelayUs),
                      ],
                    ]
                  : []),
                [
                  'Retained configuration byte',
                  value(heater.retainedConfigByte),
                ],
                ['Response byte 18 (raw)', value(heater.identityFailures)],
                [
                  'Response byte 20 / reserved 21',
                  `${value(heater.constant20)} / ${value(heater.reserved21)}`,
                ],
              ]}
            />
            {state.fault && faultDetail && (
              <Box sx={{ my: 1 }}>
                <Typography variant="subtitle2">First serial fault</Typography>
                <NameValueList
                  list={[
                    [
                      'Side / kind',
                      `${faultDetail.side} / ${faultDetail.kind}`,
                    ],
                    ['At uptime', value(faultDetail.atUs, 0, 'μs')],
                    [
                      'Expected / received byte',
                      `${byte(faultDetail.expectedByte)} / ${byte(faultDetail.receivedByte)}`,
                    ],
                    [
                      'Echo position / pending',
                      `${faultDetail.echoPosition} / ${faultDetail.echoPending}`,
                    ],
                    [
                      'Echo deadline',
                      value(faultDetail.echoDeadlineUs, 0, 'μs'),
                    ],
                    [
                      'Buffered RX / queued events / RX level',
                      `${faultDetail.bufferedRx} / ${faultDetail.queuedEvents} / ${faultDetail.rxLevel}`,
                    ],
                    ['Worker gap', value(faultDetail.workerGapUs, 0, 'μs')],
                    ...(faultDetail.stepAgeUs !== undefined
                      ? [
                          [
                            'Worker step age',
                            value(faultDetail.stepAgeUs, 0, 'μs'),
                          ],
                        ]
                      : []),
                    ...(faultDetail.rxReadAtUs !== undefined ||
                    faultDetail.rxReadBytes !== undefined
                      ? [
                          [
                            'Last UART read (uptime / bytes)',
                            `${value(faultDetail.rxReadAtUs, 0, 'μs')} / ${value(faultDetail.rxReadBytes)}`,
                          ],
                        ]
                      : []),
                  ]}
                />
              </Box>
            )}
            <Typography
              variant="caption"
              component="div"
              sx={{ overflowWrap: 'anywhere' }}
            >
              Display request: {display.requestHex || '—'}
            </Typography>
            <Typography
              variant="caption"
              component="div"
              sx={{ overflowWrap: 'anywhere' }}
            >
              ESP submitted request (attempted):{' '}
              {control.emittedRequestHex || '—'}
            </Typography>
            <Typography
              variant="caption"
              component="div"
              sx={{ overflowWrap: 'anywhere' }}
            >
              Heater response: {heater.responseHex || '—'}
            </Typography>
          </Box>
        </Stack>
      </DialogContent>
      <DialogActions>
        <Button onClick={onClose}>Close</Button>
      </DialogActions>
    </Dialog>
  );
};
