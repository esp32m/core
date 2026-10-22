# Portable managed log storage tests

Run `node tests/log-storage/run.mjs` from the shared component root. The runner
compiles the production `src/log/storage.cpp` with C++17 and uses temporary host
directories. Set `CXX` if the default host compiler is unavailable.

Coverage includes rolling byte quotas, active-segment quota reduction, reserved
filesystem space, snapshot endpoints and pins, age expiry without new records,
unknown clocks, restart metadata, partial-tail recovery, unrelated-file ownership,
strict read bounds/IDs, multi-MiB quota utilization within the hard catalog bound,
and unavailable filesystem/ID-supplier failures. Linker wrappers also inject short
writes and failed flush/close/unlink operations against the production engine.

Percentage-policy coverage checks a 50% quota against free space with owned logs
reclaimed, stable budgets during own writes/pruning/reboot, external-data growth
and shrinkage, 1%/100% modes, reserve ceilings, low-budget capture refusal,
validation, overflow-safe arithmetic, and unchanged legacy fixed-byte policies.

Host filesystems do not validate ESP32 flash timing, SPIFFS/LittleFS durability,
power-loss behavior, or hardware RAM/stack use.
