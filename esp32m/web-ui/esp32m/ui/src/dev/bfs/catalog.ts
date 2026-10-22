// Generated from bfs/parameters.hpp; run tests/bfs-parameters/catalog.mjs --write.
import { TParameterDescriptor } from './types';

export const ParameterCatalog: TParameterDescriptor[] = [
  {
    "address": 0,
    "id": "F0-00",
    "name": "Pressure drop for starting",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 1,
    "id": "F0-01",
    "name": "No-water pressure threshold",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 2,
    "id": "F0-02",
    "name": "Water-shortage protection delay",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "s",
    "scale": 1,
    "min": 1,
    "max": 600,
    "step": 1
  },
  {
    "address": 3,
    "id": "F0-03",
    "name": "Permitted fluctuation for stopping",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 0.1,
    "min": 0,
    "max": 10,
    "step": 0.1,
    "reason": "Physical unit is unspecified"
  },
  {
    "address": 4,
    "id": "F0-04",
    "name": "Pressure sensor upper range",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 1,
    "max": 60,
    "step": 0.1
  },
  {
    "address": 5,
    "id": "F0-05",
    "name": "Minimum pressure setpoint limit",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 6,
    "id": "F0-06",
    "name": "Maximum pressure setpoint limit",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 7,
    "id": "F0-07",
    "name": "Pressure setpoint adjustment",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Disabled"
      },
      {
        "value": 1,
        "label": "Enabled"
      }
    ]
  },
  {
    "address": 8,
    "id": "F0-08",
    "name": "Minimum stopping frequency",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "Hz",
    "scale": 0.01,
    "min": 0,
    "max": 300,
    "step": 0.01
  },
  {
    "address": 9,
    "id": "F0-09",
    "name": "PID acceleration coefficient",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 1,
    "max": 99,
    "step": 1
  },
  {
    "address": 10,
    "id": "F0-10",
    "name": "PID deceleration coefficient",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 1,
    "max": 99,
    "step": 1
  },
  {
    "address": 11,
    "id": "F0-11",
    "name": "Stop and zero-frequency behavior",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 2,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Maintain stop frequency"
      },
      {
        "value": 1,
        "label": "Allow stopping"
      },
      {
        "value": 2,
        "label": "Zero-frequency operation"
      }
    ]
  },
  {
    "address": 12,
    "id": "F0-12",
    "name": "Restore factory parameters",
    "group": "F0 - Pump control and pressure",
    "writable": false,
    "reason": "Factory reset is an untested action"
  },
  {
    "address": 13,
    "id": "F0-13",
    "name": "Anti-seize interval",
    "group": "F0 - Pump control and pressure",
    "writable": false,
    "unit": "h",
    "scale": 1,
    "min": 0,
    "max": 72,
    "step": 1,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 14,
    "id": "F0-14",
    "name": "Operating mode",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Constant pressure"
      },
      {
        "value": 1,
        "label": "Speed"
      }
    ]
  },
  {
    "address": 15,
    "id": "F0-15",
    "name": "Frequency source in speed mode",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 2,
    "step": 1,
    "reason": "Only documented range 0..2 is supported",
    "options": [
      {
        "value": 0,
        "label": "Panel"
      },
      {
        "value": 1,
        "label": "AI1"
      },
      {
        "value": 2,
        "label": "Communication"
      }
    ]
  },
  {
    "address": 16,
    "id": "F0-16",
    "name": "Sleep delay",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 17,
    "id": "F0-17",
    "name": "PID feedback loss threshold",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 18,
    "id": "F0-18",
    "name": "PID feedback loss delay",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 19,
    "id": "F0-19",
    "name": "Pressure sensor type",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "4-20 mA"
      },
      {
        "value": 1,
        "label": "0-5 V"
      }
    ]
  },
  {
    "address": 20,
    "id": "F0-20",
    "name": "Automatic start",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "reason": "Automatic-start conditions are unspecified",
    "options": [
      {
        "value": 0,
        "label": "Disabled"
      },
      {
        "value": 1,
        "label": "Enabled"
      }
    ]
  },
  {
    "address": 21,
    "id": "F0-21",
    "name": "PID proportional gain Kp",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 9999,
    "step": 1
  },
  {
    "address": 22,
    "id": "F0-22",
    "name": "PID integral time Ki",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "scale": 0.01,
    "min": 0,
    "max": 99.99,
    "step": 0.01,
    "reason": "Time unit is unspecified"
  },
  {
    "address": 23,
    "id": "0x0017",
    "name": "Pressure setpoint",
    "group": "F0 - Pump control and pressure",
    "writable": true,
    "unit": "bar",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1,
    "reason": "Verified FC03/FC06; limited by F0-05 and F0-06; persistence untested"
  },
  {
    "address": 256,
    "id": "F1-00",
    "name": "Motor direction",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "reason": "Pump suitability for reversal is unverified",
    "options": [
      {
        "value": 0,
        "label": "Forward"
      },
      {
        "value": 1,
        "label": "Reverse"
      }
    ]
  },
  {
    "address": 257,
    "id": "F1-01",
    "name": "PWM carrier frequency",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "kHz",
    "scale": 0.1,
    "min": 2,
    "max": 20,
    "step": 0.1
  },
  {
    "address": 258,
    "id": "F1-02",
    "name": "Acceleration time",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 60,
    "step": 0.1
  },
  {
    "address": 259,
    "id": "F1-03",
    "name": "Deceleration time",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 60,
    "step": 0.1
  },
  {
    "address": 260,
    "id": "F1-04",
    "name": "Operating frequency upper limit",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "Hz",
    "scale": 0.01,
    "min": 0,
    "max": 300,
    "step": 0.01
  },
  {
    "address": 261,
    "id": "F1-05",
    "name": "Rated motor current",
    "group": "F1 - Motor and soft-start",
    "writable": false,
    "unit": "A",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1,
    "reason": "The model-dependent limit is not documented"
  },
  {
    "address": 262,
    "id": "F1-06",
    "name": "Motor no-load current",
    "group": "F1 - Motor and soft-start",
    "writable": false,
    "unit": "A",
    "scale": 0.1,
    "min": 0.1,
    "max": 6553.5,
    "step": 0.1,
    "reason": "The model-dependent limit is not documented"
  },
  {
    "address": 263,
    "id": "F1-07",
    "name": "Rated motor voltage",
    "group": "F1 - Motor and soft-start",
    "writable": false,
    "unit": "V",
    "scale": 1,
    "min": 1,
    "max": 65535,
    "step": 1,
    "reason": "The model-dependent limit is not documented"
  },
  {
    "address": 264,
    "id": "F1-08",
    "name": "Continuous operation time limit",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "h",
    "scale": 1,
    "min": 0,
    "max": 255,
    "step": 1
  },
  {
    "address": 265,
    "id": "F1-09",
    "name": "Motor oscillation suppression coefficient",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "scale": 0.01,
    "min": 0,
    "max": 10,
    "step": 0.01
  },
  {
    "address": 266,
    "id": "F1-10",
    "name": "Maximum output frequency",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "Hz",
    "scale": 0.01,
    "min": 0,
    "max": 300,
    "step": 0.01
  },
  {
    "address": 267,
    "id": "F1-11",
    "name": "Rated motor frequency",
    "group": "F1 - Motor and soft-start",
    "writable": true,
    "unit": "Hz",
    "scale": 0.01,
    "min": 0,
    "max": 300,
    "step": 0.01
  },
  {
    "address": 512,
    "id": "F2-00",
    "name": "Running undervoltage threshold",
    "group": "F2 - Protections",
    "writable": false,
    "unit": "V",
    "scale": 1,
    "min": 0,
    "max": 65535,
    "step": 1,
    "reason": "Lower limit FF03 and the applicable model bounds are unknown"
  },
  {
    "address": 513,
    "id": "F2-01",
    "name": "Overvoltage threshold",
    "group": "F2 - Protections",
    "writable": false,
    "unit": "V",
    "scale": 1,
    "min": 0,
    "max": 65535,
    "step": 1,
    "reason": "Applicable 220 V or 380 V model bounds need confirmation"
  },
  {
    "address": 514,
    "id": "F2-02",
    "name": "Heatsink temperature threshold",
    "group": "F2 - Protections",
    "writable": false,
    "reason": "Temperature scale is unknown"
  },
  {
    "address": 515,
    "id": "F2-03",
    "name": "Overtemperature setting",
    "group": "F2 - Protections",
    "writable": false,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 516,
    "id": "F2-04",
    "name": "Input/output phase-loss protection",
    "group": "F2 - Protections",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 3,
    "step": 1,
    "reason": "Raw selector 0..3; option meanings are not fully verified"
  },
  {
    "address": 517,
    "id": "F2-05",
    "name": "Phase-loss protection delay",
    "group": "F2 - Protections",
    "writable": false,
    "unit": "s",
    "scale": 0.1,
    "min": 0,
    "max": 30,
    "step": 0.1,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 518,
    "id": "F2-06",
    "name": "Overload protection coefficient",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "%",
    "scale": 1,
    "min": 30,
    "max": 120,
    "step": 1
  },
  {
    "address": 519,
    "id": "F2-07",
    "name": "Anti-freeze operation duration",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "min",
    "scale": 1,
    "min": 0,
    "max": 255,
    "step": 1
  },
  {
    "address": 520,
    "id": "F2-08",
    "name": "Anti-freeze frequency",
    "group": "F2 - Protections",
    "writable": false,
    "reason": "Frequency scale is unknown"
  },
  {
    "address": 521,
    "id": "F2-09",
    "name": "Anti-freeze interval",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "min",
    "scale": 1,
    "min": 0,
    "max": 255,
    "step": 1
  },
  {
    "address": 522,
    "id": "F2-10",
    "name": "Overpressure warning threshold",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "% sensor span",
    "scale": 0.1,
    "min": 0,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 523,
    "id": "F2-11",
    "name": "Overpressure warning delay",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0,
    "max": 6553.5,
    "step": 0.1
  },
  {
    "address": 524,
    "id": "F2-12",
    "name": "Pressure warning reset threshold",
    "group": "F2 - Protections",
    "writable": true,
    "unit": "% sensor span",
    "scale": 0.1,
    "min": 0,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 525,
    "id": "F2-13",
    "name": "Protection word",
    "group": "F2 - Protections",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 4401,
    "step": 1
  },
  {
    "address": 526,
    "id": "F2-14",
    "name": "Cycle-by-cycle current limit",
    "group": "F2 - Protections",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 273,
    "step": 1
  },
  {
    "address": 527,
    "id": "F2-15",
    "name": "Automatic voltage regulation",
    "group": "F2 - Protections",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 2,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Disabled"
      },
      {
        "value": 1,
        "label": "Always enabled"
      },
      {
        "value": 2,
        "label": "Disabled during deceleration"
      }
    ]
  },
  {
    "address": 768,
    "id": "F3-00",
    "name": "Multi-pump linked mode",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Standalone"
      },
      {
        "value": 1,
        "label": "Linked"
      }
    ]
  },
  {
    "address": 769,
    "id": "F3-01",
    "name": "Pump addition delay",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 770,
    "id": "F3-02",
    "name": "Pump removal delay",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 771,
    "id": "F3-03",
    "name": "Pump reduction frequency threshold",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "Hz",
    "scale": 0.01,
    "min": 1,
    "max": 300,
    "step": 0.01
  },
  {
    "address": 772,
    "id": "F3-04",
    "name": "Multi-pump group address",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "scale": 1,
    "min": 1,
    "max": 16,
    "step": 1
  },
  {
    "address": 773,
    "id": "F3-05",
    "name": "Pump alternation interval",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "h",
    "scale": 1,
    "min": 0,
    "max": 255,
    "step": 1
  },
  {
    "address": 774,
    "id": "F3-06",
    "name": "RUN/STOP source",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "reason": "Only documented range 0..1 is supported",
    "options": [
      {
        "value": 0,
        "label": "Panel"
      },
      {
        "value": 1,
        "label": "RS485"
      }
    ]
  },
  {
    "address": 778,
    "id": "F3-10",
    "name": "Slave response delay",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "ms",
    "scale": 1,
    "min": 0,
    "max": 200,
    "step": 1
  },
  {
    "address": 779,
    "id": "F3-11",
    "name": "Communication timeout",
    "group": "F3 - Multi-pump and RS485",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 100,
    "step": 0.1
  },
  {
    "address": 780,
    "id": "F3-12",
    "name": "Compatible protocol",
    "group": "F3 - Multi-pump and RS485",
    "writable": false,
    "scale": 1,
    "min": 0,
    "max": 1,
    "step": 1,
    "reason": "Alternative protocol and switching are unverified",
    "options": [
      {
        "value": 0,
        "label": "MD380"
      },
      {
        "value": 1,
        "label": "Delta-M"
      }
    ]
  },
  {
    "address": 1024,
    "id": "F4-00",
    "name": "AVI lower input",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "V",
    "scale": 0.01,
    "min": 0,
    "max": 5,
    "step": 0.01
  },
  {
    "address": 1025,
    "id": "F4-01",
    "name": "AVI lower mapped value",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "unit": "%",
    "scale": 0.1,
    "min": -100,
    "max": 100,
    "step": 0.1,
    "reason": "Negative two's-complement encoding is unverified",
    "inferred": true,
    "signed": true
  },
  {
    "address": 1026,
    "id": "F4-02",
    "name": "AVI upper input",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "V",
    "scale": 0.01,
    "min": 0,
    "max": 5,
    "step": 0.01
  },
  {
    "address": 1027,
    "id": "F4-03",
    "name": "AVI upper mapped value",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "unit": "%",
    "scale": 0.1,
    "min": -100,
    "max": 100,
    "step": 0.1,
    "reason": "Negative two's-complement encoding is unverified",
    "inferred": true,
    "signed": true
  },
  {
    "address": 1028,
    "id": "F4-04",
    "name": "AVI filter time constant",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 5,
    "step": 0.1
  },
  {
    "address": 1029,
    "id": "F4-05",
    "name": "ACI lower input",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "mA",
    "scale": 0.01,
    "min": 0,
    "max": 20,
    "step": 0.01
  },
  {
    "address": 1030,
    "id": "F4-06",
    "name": "ACI lower mapped value",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "unit": "%",
    "scale": 0.1,
    "min": -100,
    "max": 100,
    "step": 0.1,
    "reason": "Negative two's-complement encoding is unverified",
    "inferred": true,
    "signed": true
  },
  {
    "address": 1031,
    "id": "F4-07",
    "name": "ACI upper input",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "mA",
    "scale": 0.01,
    "min": 0,
    "max": 20,
    "step": 0.01
  },
  {
    "address": 1032,
    "id": "F4-08",
    "name": "ACI upper mapped value",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "unit": "%",
    "scale": 0.1,
    "min": -100,
    "max": 100,
    "step": 0.1,
    "reason": "Negative two's-complement encoding is unverified",
    "inferred": true,
    "signed": true
  },
  {
    "address": 1033,
    "id": "F4-09",
    "name": "ACI filter time constant",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0.1,
    "max": 5,
    "step": 0.1
  },
  {
    "address": 1034,
    "id": "F4-10",
    "name": "X1 function",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 6,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Unused"
      },
      {
        "value": 1,
        "label": "Forward control"
      },
      {
        "value": 2,
        "label": "Reverse control"
      },
      {
        "value": 3,
        "label": "Frequency UP"
      },
      {
        "value": 4,
        "label": "Frequency DOWN"
      },
      {
        "value": 5,
        "label": "External fault, normally open"
      },
      {
        "value": 6,
        "label": "Clear UP/DOWN frequency"
      }
    ]
  },
  {
    "address": 1035,
    "id": "F4-11",
    "name": "Reserved",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 1036,
    "id": "F4-12",
    "name": "X1 filter coefficient",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "scale": 0.001,
    "min": 0,
    "max": 1,
    "step": 0.001,
    "reason": "Physical unit is unspecified"
  },
  {
    "address": 1037,
    "id": "F4-13",
    "name": "R1 relay function",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "scale": 1,
    "min": 0,
    "max": 3,
    "step": 1,
    "options": [
      {
        "value": 0,
        "label": "Unused"
      },
      {
        "value": 1,
        "label": "Running"
      },
      {
        "value": 2,
        "label": "Fault"
      },
      {
        "value": 3,
        "label": "Zero-speed running"
      }
    ]
  },
  {
    "address": 1038,
    "id": "F4-14",
    "name": "Reserved",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 1039,
    "id": "F4-15",
    "name": "R1 delay",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": true,
    "unit": "s",
    "scale": 0.1,
    "min": 0,
    "max": 250,
    "step": 0.1
  },
  {
    "address": 1040,
    "id": "F4-16",
    "name": "Reserved",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "reason": "Reserved; implementation unknown"
  },
  {
    "address": 1041,
    "id": "F4-17",
    "name": "UP/DOWN frequency change rate",
    "group": "F4 - Analog inputs, X1 and relay",
    "writable": false,
    "unit": "Hz/s",
    "scale": 0.01,
    "min": 0,
    "max": 655.35,
    "step": 0.01,
    "reason": "Maximum limit has inconsistent units in the source table"
  }
];
