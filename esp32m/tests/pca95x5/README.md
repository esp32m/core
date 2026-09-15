# PCA95x5 regression tests

Run from the parent workspace:

```powershell
node core/esp32m/tests/pca95x5/run.mjs
```

The runner compiles unchanged production PCA declarations/methods and the actual
GPIO Tx implementation with fake I2C/platform services. It uses the installed
MinGW compiler (or CXX), a verified temporary build directory, and no hardware.

Coverage includes safe latch-before-direction reset recovery; no interrupted
motion replay; failed startup retry; uncertain direction/output writes; aborted
partial batches; input snapshots across two providers; concurrent fan versus
motor writes; multi-expander commit; inversion reset; pin bounds; null device;
eight-bit error propagation; and PCA9535C electrical capability metadata.

Automatic recovery only uses application-configured safe levels. An incomplete
policy blocks mismatched hardware rather than guessing levels. Exact unchanged
readback after a rejected write permits a fresh operation, while the interrupted
command is reported and discarded. Successful writes are acknowledged by I2C;
state is verified before subsequent operations and recovery additionally checks
its register writes immediately. These tests do not establish physical motor
stop, timing under the RTOS, or electrical-noise tolerance.
