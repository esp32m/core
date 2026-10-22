#pragma once

#include <cstddef>
#include <cstdint>

namespace esp32m {
  namespace dev {
    namespace bfs {

      struct Option {
        uint16_t value;
        const char* label;
      };

      struct Parameter {
        uint16_t address;
        const char *id, *name, *group, *unit;
        // Zero means unknown scale. Bounds are raw; dependent limits need a
        // fresh device read before writing and are enforced by the driver.
        double scale;
        int32_t min, max;
        bool writable, inferred, signedValue;
        const char* reason;
        const Option* options;
        size_t optionCount;
        const char* evidence = "R/D";
        bool stopRequired = true;
        bool persistenceKnown = false;
      };

      inline constexpr const char* Pump = "F0 - Pump control and pressure";
      inline constexpr const char* Motor = "F1 - Motor and soft-start";
      inline constexpr const char* Protection = "F2 - Protections";
      inline constexpr const char* Communication = "F3 - Multi-pump and RS485";
      inline constexpr const char* Inputs = "F4 - Analog inputs, X1 and relay";

      inline constexpr Option OnOff[] = {{0, "Disabled"}, {1, "Enabled"}};
      inline constexpr Option StopBehavior[] = {
          {0, "Maintain stop frequency"}, {1, "Allow stopping"},
          {2, "Zero-frequency operation"}};
      inline constexpr Option Mode[] = {{0, "Constant pressure"}, {1, "Speed"}};
      inline constexpr Option FrequencySource[] = {
          {0, "Panel"}, {1, "AI1"}, {2, "Communication"}};
      inline constexpr Option SensorType[] = {{0, "4-20 mA"}, {1, "0-5 V"}};
      inline constexpr Option Direction[] = {{0, "Forward"}, {1, "Reverse"}};
      inline constexpr Option Avr[] = {
          {0, "Disabled"}, {1, "Always enabled"},
          {2, "Disabled during deceleration"}};
      inline constexpr Option Linked[] = {{0, "Standalone"}, {1, "Linked"}};
      inline constexpr Option RunSource[] = {{0, "Panel"}, {1, "RS485"}};
      inline constexpr Option Protocol[] = {{0, "MD380"}, {1, "Delta-M"}};
      inline constexpr Option X1[] = {
          {0, "Unused"}, {1, "Forward control"}, {2, "Reverse control"},
          {3, "Frequency UP"}, {4, "Frequency DOWN"},
          {5, "External fault, normally open"},
          {6, "Clear UP/DOWN frequency"}};
      inline constexpr Option Relay[] = {
          {0, "Unused"}, {1, "Running"}, {2, "Fault"},
          {3, "Zero-speed running"}};

      inline constexpr const char* Reserved = "Reserved; implementation unknown";
      inline constexpr const char* ModelLimit =
          "The model-dependent limit is not documented";
      inline constexpr const char* SignedEncoding =
          "Negative two's-complement encoding is unverified";

      // BFS Modbus API v1.1, 2026-09-27. Indices after '-' are decimal.
      // F3-07..09 belong to communication settings, outside this allowlist.
      inline constexpr Parameter Parameters[] = {
          {0x0000, "F0-00", "Pressure drop for starting", Pump, "bar", .1,
           1, 65535, true, false, false, nullptr, nullptr, 0},
          {0x0001, "F0-01", "No-water pressure threshold", Pump, "bar", .1,
           0, 65535, true, false, false, nullptr, nullptr, 0},
          {0x0002, "F0-02", "Water-shortage protection delay", Pump, "s", 1,
           1, 600, true, false, false, nullptr, nullptr, 0},
          {0x0003, "F0-03", "Permitted fluctuation for stopping", Pump,
           nullptr, .1, 0, 100, true, false, false,
           "Physical unit is unspecified", nullptr, 0},
          {0x0004, "F0-04", "Pressure sensor upper range", Pump, "bar", .1,
           10, 600, true, false, false, nullptr, nullptr, 0},
          {0x0005, "F0-05", "Minimum pressure setpoint limit", Pump, "bar",
           .1, 1, 65535, true, false, false, nullptr, nullptr, 0},
          {0x0006, "F0-06", "Maximum pressure setpoint limit", Pump, "bar",
           .1, 1, 65535, true, false, false, nullptr, nullptr, 0},
          {0x0007, "F0-07", "Pressure setpoint adjustment", Pump, nullptr,
           1, 0, 1, true, false, false, nullptr, OnOff, 2},
          {0x0008, "F0-08", "Minimum stopping frequency", Pump, "Hz", .01,
           0, 30000, true, false, false, nullptr, nullptr, 0},
          {0x0009, "F0-09", "PID acceleration coefficient", Pump, nullptr,
           1, 1, 99, true, false, false, nullptr, nullptr, 0},
          {0x000A, "F0-10", "PID deceleration coefficient", Pump, nullptr,
           1, 1, 99, true, false, false, nullptr, nullptr, 0},
          {0x000B, "F0-11", "Stop and zero-frequency behavior", Pump,
           nullptr, 1, 0, 2, true, false, false, nullptr, StopBehavior, 3},
          {0x000C, "F0-12", "Restore factory parameters", Pump, nullptr, 0,
           0, 1, false, false, false, "Factory reset is an untested action",
           nullptr, 0},
          {0x000D, "F0-13", "Anti-seize interval", Pump, "h", 1, 0, 72,
           false, false, false, Reserved, nullptr, 0},
          {0x000E, "F0-14", "Operating mode", Pump, nullptr, 1, 0, 1,
           true, false, false, nullptr, Mode, 2, "R/D/W"},
          {0x000F, "F0-15", "Frequency source in speed mode", Pump,
           nullptr, 1, 0, 2, true, false, false,
           "Only documented range 0..2 is supported", FrequencySource, 3,
           "R/D/W"},
          {0x0010, "F0-16", "Sleep delay", Pump, "s", .1, 0, 1000, true,
           false, false, nullptr, nullptr, 0},
          {0x0011, "F0-17", "PID feedback loss threshold", Pump, "bar", .1,
           0, 65535, true, false, false, nullptr, nullptr, 0},
          {0x0012, "F0-18", "PID feedback loss delay", Pump, "s", .1, 0,
           1000, true, false, false, nullptr, nullptr, 0},
          {0x0013, "F0-19", "Pressure sensor type", Pump, nullptr, 1, 0, 1,
           true, false, false, nullptr, SensorType, 2},
          {0x0014, "F0-20", "Automatic start", Pump, nullptr, 1, 0, 1,
           true, false, false, "Automatic-start conditions are unspecified",
           OnOff, 2},
          {0x0015, "F0-21", "PID proportional gain Kp", Pump, nullptr, 1,
           0, 9999, true, false, false, nullptr, nullptr, 0},
          {0x0016, "F0-22", "PID integral time Ki", Pump, nullptr, .01, 0,
           9999, true, false, false, "Time unit is unspecified", nullptr, 0},
          {0x0017, "0x0017", "Pressure setpoint", Pump, "bar", .1, 1,
           65535, true, false, false,
           "Verified FC03/FC06; limited by F0-05 and F0-06; persistence untested",
           nullptr, 0, "R/W 2026-10-09", false},

          {0x0100, "F1-00", "Motor direction", Motor, nullptr, 1, 0, 1,
           true, false, false, "Pump suitability for reversal is unverified",
           Direction, 2},
          {0x0101, "F1-01", "PWM carrier frequency", Motor, "kHz", .1, 20,
           200, true, false, false, nullptr, nullptr, 0},
          {0x0102, "F1-02", "Acceleration time", Motor, "s", .1, 1, 600,
           true, false, false, nullptr, nullptr, 0},
          {0x0103, "F1-03", "Deceleration time", Motor, "s", .1, 1, 600,
           true, false, false, nullptr, nullptr, 0},
          {0x0104, "F1-04", "Operating frequency upper limit", Motor,
           "Hz", .01, 0, 30000, true, false, false, nullptr, nullptr, 0},
          {0x0105, "F1-05", "Rated motor current", Motor, "A", .1, 1,
           65535, false, false, false, ModelLimit, nullptr, 0, "R/D/W", true,
           true},
          {0x0106, "F1-06", "Motor no-load current", Motor, "A", .1, 1,
           65535, false, false, false, ModelLimit, nullptr, 0},
          {0x0107, "F1-07", "Rated motor voltage", Motor, "V", 1, 1,
           65535, false, false, false, ModelLimit, nullptr, 0},
          {0x0108, "F1-08", "Continuous operation time limit", Motor, "h",
           1, 0, 255, true, false, false, nullptr, nullptr, 0},
          {0x0109, "F1-09", "Motor oscillation suppression coefficient",
           Motor, nullptr, .01, 0, 1000, true, false, false, nullptr,
           nullptr, 0},
          {0x010A, "F1-10", "Maximum output frequency", Motor, "Hz", .01,
           0, 30000, true, false, false, nullptr, nullptr, 0},
          {0x010B, "F1-11", "Rated motor frequency", Motor, "Hz", .01, 0,
           30000, true, false, false, nullptr, nullptr, 0},

          {0x0200, "F2-00", "Running undervoltage threshold", Protection,
           "V", 1, 0, 65535, false, false, false,
           "Lower limit FF03 and the applicable model bounds are unknown",
           nullptr, 0},
          {0x0201, "F2-01", "Overvoltage threshold", Protection, "V", 1,
           0, 65535, false, false, false,
           "Applicable 220 V or 380 V model bounds need confirmation",
           nullptr, 0},
          {0x0202, "F2-02", "Heatsink temperature threshold", Protection,
           nullptr, 0, 0, 65535, false, false, false,
           "Temperature scale is unknown", nullptr, 0},
          {0x0203, "F2-03", "Overtemperature setting", Protection, nullptr,
           0, 0, 65535, false, false, false, Reserved, nullptr, 0},
          {0x0204, "F2-04", "Input/output phase-loss protection", Protection,
           nullptr, 1, 0, 3, true, false, false,
           "Raw selector 0..3; option meanings are not fully verified",
           nullptr, 0},
          {0x0205, "F2-05", "Phase-loss protection delay", Protection, "s",
           .1, 0, 300, false, false, false, Reserved, nullptr, 0},
          {0x0206, "F2-06", "Overload protection coefficient", Protection,
           "%", 1, 30, 120, true, false, false, nullptr, nullptr, 0},
          {0x0207, "F2-07", "Anti-freeze operation duration", Protection,
           "min", 1, 0, 255, true, false, false, nullptr, nullptr, 0},
          {0x0208, "F2-08", "Anti-freeze frequency", Protection, nullptr,
           0, 0, 65535, false, false, false, "Frequency scale is unknown",
           nullptr, 0},
          {0x0209, "F2-09", "Anti-freeze interval", Protection, "min", 1,
           0, 255, true, false, false, nullptr, nullptr, 0},
          {0x020A, "F2-10", "Overpressure warning threshold", Protection,
           "% sensor span", .1, 0, 1000, true, false, false, nullptr,
           nullptr, 0},
          {0x020B, "F2-11", "Overpressure warning delay", Protection, "s",
           .1, 0, 65535, true, false, false, nullptr, nullptr, 0},
          {0x020C, "F2-12", "Pressure warning reset threshold", Protection,
           "% sensor span", .1, 0, 1000, true, false, false, nullptr,
           nullptr, 0},
          {0x020D, "F2-13", "Protection word", Protection, nullptr, 1, 0,
           0x1131, true, false, false, nullptr, nullptr, 0, "R/D/W"},
          {0x020E, "F2-14", "Cycle-by-cycle current limit", Protection,
           nullptr, 1, 0, 0x0111, true, false, false, nullptr, nullptr, 0},
          {0x020F, "F2-15", "Automatic voltage regulation", Protection,
           nullptr, 1, 0, 2, true, false, false, nullptr, Avr, 3},

          {0x0300, "F3-00", "Multi-pump linked mode", Communication,
           nullptr, 1, 0, 1, true, false, false, nullptr, Linked, 2},
          {0x0301, "F3-01", "Pump addition delay", Communication, "s", .1,
           1, 1000, true, false, false, nullptr, nullptr, 0},
          {0x0302, "F3-02", "Pump removal delay", Communication, "s", .1,
           1, 1000, true, false, false, nullptr, nullptr, 0},
          {0x0303, "F3-03", "Pump reduction frequency threshold",
           Communication, "Hz", .01, 100, 30000, true, false, false,
           nullptr, nullptr, 0},
          {0x0304, "F3-04", "Multi-pump group address", Communication,
           nullptr, 1, 1, 16, true, false, false, nullptr, nullptr, 0},
          {0x0305, "F3-05", "Pump alternation interval", Communication,
           "h", 1, 0, 255, true, false, false, nullptr, nullptr, 0},
          {0x0306, "F3-06", "RUN/STOP source", Communication, nullptr, 1,
           0, 1, true, false, false,
           "Only documented range 0..1 is supported", RunSource, 2,
           "R/D/W"},
          {0x030A, "F3-10", "Slave response delay", Communication, "ms", 1,
           0, 200, true, false, false, nullptr, nullptr, 0, "R/D/W"},
          {0x030B, "F3-11", "Communication timeout", Communication, "s",
           .1, 1, 1000, true, false, false, nullptr, nullptr, 0, "R/D/W"},
          {0x030C, "F3-12", "Compatible protocol", Communication, nullptr,
           1, 0, 1, false, false, false,
           "Alternative protocol and switching are unverified", Protocol, 2},

          {0x0400, "F4-00", "AVI lower input", Inputs, "V", .01, 0, 500,
           true, false, false, nullptr, nullptr, 0},
          {0x0401, "F4-01", "AVI lower mapped value", Inputs, "%", .1,
           -1000, 1000, false, true, true, SignedEncoding, nullptr, 0, "R/D/H"},
          {0x0402, "F4-02", "AVI upper input", Inputs, "V", .01, 0, 500,
           true, false, false, nullptr, nullptr, 0},
          {0x0403, "F4-03", "AVI upper mapped value", Inputs, "%", .1,
           -1000, 1000, false, true, true, SignedEncoding, nullptr, 0, "R/D/H"},
          {0x0404, "F4-04", "AVI filter time constant", Inputs, "s", .1,
           1, 50, true, false, false, nullptr, nullptr, 0},
          {0x0405, "F4-05", "ACI lower input", Inputs, "mA", .01, 0,
           2000, true, false, false, nullptr, nullptr, 0},
          {0x0406, "F4-06", "ACI lower mapped value", Inputs, "%", .1,
           -1000, 1000, false, true, true, SignedEncoding, nullptr, 0, "R/D/H"},
          {0x0407, "F4-07", "ACI upper input", Inputs, "mA", .01, 0,
           2000, true, false, false, nullptr, nullptr, 0},
          {0x0408, "F4-08", "ACI upper mapped value", Inputs, "%", .1,
           -1000, 1000, false, true, true, SignedEncoding, nullptr, 0, "R/D/H"},
          {0x0409, "F4-09", "ACI filter time constant", Inputs, "s", .1,
           1, 50, true, false, false, nullptr, nullptr, 0},
          {0x040A, "F4-10", "X1 function", Inputs, nullptr, 1, 0, 6, true,
           false, false, nullptr, X1, 7},
          {0x040B, "F4-11", "Reserved", Inputs, nullptr, 0, 0, 0, false,
           false, false, Reserved, nullptr, 0},
          {0x040C, "F4-12", "X1 filter coefficient", Inputs, nullptr, .001,
           0, 1000, true, false, false, "Physical unit is unspecified",
           nullptr, 0},
          {0x040D, "F4-13", "R1 relay function", Inputs, nullptr, 1, 0, 3,
           true, false, false, nullptr, Relay, 4},
          {0x040E, "F4-14", "Reserved", Inputs, nullptr, 0, 0, 0, false,
           false, false, Reserved, nullptr, 0},
          {0x040F, "F4-15", "R1 delay", Inputs, "s", .1, 0, 2500, true,
           false, false, nullptr, nullptr, 0},
          {0x0410, "F4-16", "Reserved", Inputs, nullptr, 0, 0, 0, false,
           false, false, Reserved, nullptr, 0},
          {0x0411, "F4-17", "UP/DOWN frequency change rate", Inputs,
           "Hz/s", .01, 0, 65535, false, false, false,
           "Maximum limit has inconsistent units in the source table",
           nullptr, 0},
      };

      inline constexpr size_t parametersCount =
          sizeof(Parameters) / sizeof(Parameters[0]);
      static_assert(parametersCount == 80, "BFS parameter allowlist changed");

      constexpr const Parameter* findParameter(uint16_t address) {
        for (const auto& parameter : Parameters)
          if (parameter.address == address)
            return &parameter;
        return nullptr;
      }

      constexpr bool validateRaw(const Parameter& parameter, uint16_t raw) {
        if (!parameter.writable || parameter.scale <= 0 ||
            parameter.signedValue || raw < parameter.min || raw > parameter.max)
          return false;
        if (parameter.address == 0x020D)
          return (raw & 0x000F) <= 1 && ((raw >> 4) & 0x000F) <= 3 &&
                 ((raw >> 8) & 0x000F) <= 1 && (raw >> 12) <= 1;
        if (parameter.address == 0x020E)
          return (raw & ~0x0111u) == 0;
        if (parameter.optionCount) {
          for (size_t i = 0; i < parameter.optionCount; ++i)
            if (parameter.options[i].value == raw)
              return true;
          return false;
        }
        return true;
      }

    }  // namespace bfs
  }  // namespace dev
}  // namespace esp32m
