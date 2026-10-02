# Claude implementation brief — Train Info Panel

## Mission
Build an original **landscape UK local-departure board** for the Waveshare ESP32-C6-Touch-LCD-1.47. It should make nearby upcoming services and material disruption glanceable, even when live data fails.

Do not search for or recreate prior mock-up screenshots. Produce fresh concepts from this brief. The cited projects are information-design references, not templates.

## Fixed facts and constraints

- Display native resolution is `172 × 320`; project orientation is landscape `320 × 172`.
- ESP32-C6FH8 has 8 MB flash and no PSRAM. Keep assets, buffers and allocations disciplined.
- LCD controller is JD9853; touch is AXS5106L. Touch is optional in v1.
- This is a small visual appliance, not a journey planner or a phone replacement.
- A live-data failure must look visibly different from live data. Scheduled information can remain useful when correctly labelled.
- Do not commit credentials, personal travel data, local URLs, provider tokens or raw response captures.

## Product judgement

Borrow the grammar of a UK platform departure board: an aligned, calm list of time, destination, platform and state. Landscape is intentional because it fits one meaningful row without turning destination/status into microscopic stacked labels.

Default screen should show a few departures. A genuine cancellation, material delay or platform change may take priority temporarily. Avoid continuous scrolling, decorative animation, a mini-map, weather, irrelevant alerts, or clever-but-illegible interface furniture.

## Design exercise

Before firmware implementation, propose three original device-scale concepts. For each, assess:
1. time-to-answer at arm’s length;
2. exact `320×172` text sizing and truncation strategy;
3. whether it handles long destinations honestly;
4. colour semantics for live, warning, cancelled and stale;
5. whether it remains useful on a bad data day.

Choose one and record the decision in `docs/`.

## Technical approach

Use ESP-IDF or a well-supported PlatformIO/Arduino setup with reproducible builds. Keep the panel provider-neutral by defining a compact normalised payload and an adapter where sensible.

Create sanitised fixtures for:

- live normal departures;
- delayed service;
- cancellation or platform change;
- scheduled fallback;
- stale/malformed/no-network response.

The firmware should have bounded timeouts/retry/backoff, last-good caching with a visible age, and no unauthenticated network-control surface. Preserve native USB recovery pins. Test physical panel output, not only browser emulation.

## References

- UKDepartureBoards / UK departure-board conventions: aligned facts and conservative disruption language.
- https://github.com/davwheat/led-departure-board
- https://github.com/chrisys/train-departure-display
- https://www.nationalrail.co.uk/developers/
- https://docs.waveshare.com/ESP32-C6-Touch-LCD-1.47

Learn from these sources, but do not copy their source, layouts, screenshots, station branding or assets.

## Definition of done for a first vertical slice

- The board boots to an original, legible landscape screen using fixture data.
- All five fixture states render without clipping or false-live claims.
- There is a small host-side contract test plus on-device smoke test.
- README explains setup, flashing, recovery, data-source status and limitations.
- The route from provider data to firmware is deliberately narrow and secrets-free.
