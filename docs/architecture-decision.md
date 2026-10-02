# Architecture decision: the panel talks to LDBWS directly

**Decided 2026-10-02. Supersedes the "home-side adapter" architecture in the first draft of `docs/data-source.md`.**

```
RDM LDBWS  <--HTTPS, outbound only--  panel (ESP32-C6)      nothing else on the network
```

## Decision

The panel fetches departure boards itself, parses them on the device, and keeps everything it needs in its own flash: the LDBWS consumer key, the Wi-Fi network details and every other setting, in NVS, written over USB by `tools/provision.py` from the user's `config.ini`. No service runs anywhere else; the panel opens no listening socket.

## Why

- The consumer key can only *read* departure boards; it cannot change anything upstream. Holding it on the device exposes nothing that a leaked read-only key doesn't already.
- A second moving part (a laptop/Pi/server service that must be up, reachable and authenticated) adds failure modes and an attack surface, for no capability the panel lacks. The brief's rule of "no unauthenticated network-control surface" is satisfied most simply by having **no inbound surface at all**.
- The ESP32-C6 comfortably does a TLS request and a JSON parse of a ~2-4 KB board once a minute.

## Consequences and mitigations

- **Secrets live in flash, unencrypted** (NVS encryption would need flash encryption plus key management, and would complicate USB recovery). Anyone with the board and a USB cable can read them back. Acceptable for a read-only board key and home Wi-Fi for a panel kept at home; do not lend the panel out. `idf.py erase-flash` removes them.
- **Provider policy now lives in firmware** (route, headers, filter semantics, the User-Agent the gateway requires). It is confined to `main/net/` and `main/board/ldbws_parse.c`.
- **Two implementations of normalisation**: the device's `ldbws_parse.c` and `adapter/normalise.py`. The Python version is kept as the readable reference, the mock server's counterpart, and the oracle: the host test feeds both the same raw boards and requires identical results, and both were also compared on real live responses. The adapter is no longer part of the running system.
- **Configuration** is one user-edited file, `config.ini` (station, directions, intervals, warnings, quiet hours, Wi-Fi, key), written to the device's NVS by `tools/provision.py`, so the firmware is generic and a settings change needs no rebuild.
- **Safe behaviour kept**: 10 s request timeout, 12 KB response cap, backoff (30 s up to 5 min; 15 min if the key is refused; 10 min if rate-limited), last-good board kept with a visible, monotonic-clock age, TLS verified against the bundled CA list with redirects disabled so the key is never sent anywhere else.
- Not persisted across reboot: the last good board lives in RAM, so a rebooted panel shows "Connecting" until its first fetch, never old data presented as current.
