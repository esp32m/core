# HTTP access host tests

Run `node tests/httpd-access/run.mjs` from the core root.

The runner compiles the production URI matcher, custom HTTP/WS dispatchers, Basic
credential generator, authentication gate and shared endpoint helper. Platform
HTTP request/response calls and Base64 encoding are replaced by host adapters.
Checks cover missing, malformed, wrong, prefixed, suffixed and unreadable
Authorization values; denied requests never reach handlers. Query strings,
HTTP methods and exact `/ws` and `/ws/uart` boundaries are exercised, together
with protected WS handshakes and routing data frames by socket or URI fallback.

These checks do not validate ESP-IDF's HTTP parser, TLS or live WebSocket sessions.
