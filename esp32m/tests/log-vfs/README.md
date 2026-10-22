# Generic file appender host tests

Run `node tests/log-vfs/run.mjs` from the core root.

The runner compiles the production Vfs class and implementation plus
FormattingAppender's constructor/append methods. Filesystem adapters inject
open, stat, write, short-write, flush, close, unlink and rename failures.
Checks cover filename ownership, handle closure, ISR/OTA gates, legacy
`maxFiles <= 1` behavior, archive ordering and fail-closed rotation. Null
readiness probes are forwarded without invoking a formatter.

The host filesystem does not validate flash wear, power loss recovery or the
ESP-IDF filesystem drivers.
