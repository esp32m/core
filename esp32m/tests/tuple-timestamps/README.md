# Tuple acquisition timestamps

Run `node tests/tuple-timestamps/run.mjs` from the esp32m component root.
The harness compiles production snapshot/poll methods and real stamp field
declarations against ArduinoJson 7 with deterministic ADC, Modbus, I2C, pulse
counter, sampler and clock doubles. No device or network access occurs.

Five groups verify never-read `-1`, valid sample time zero, clocks beyond the
32-bit millisecond rollover, equal successful acquisitions advancing timestamps,
failed/skipped polls preserving them, snapshots remaining stable without a new
acquisition, and background ADC provenance. The existing public
`FlowSensor::compute(int, int)` contract and its 32-bit elapsed-counter arithmetic
are preserved independently from its 64-bit acquisition timestamp.

This test does not replace full firmware compilation or physical validation.
The complete CSF09 production-driver suite independently exercises tuple sample
timestamps alongside failure, recovery, concurrency and commissioning behavior.
