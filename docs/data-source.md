# Data source decision

**Status:** decided and **verified against the live service on 2026-10-02**. The RDM key works (the `key` in your git-ignored `config.ini`); `adapter/live_fetch.py` fetches a real board and normalises it.

## Decision

Use **LDBWS (Live Departure Board Web Service), "Public" product, via Rail Data Marketplace** (raildata.org.uk). LDBWS is the request/response front end to **Darwin**, the GB rail industry's real-time engine that also drives nationalrail.co.uk's live boards. It is free for personal use and updated continuously.

Access: free open tier (reported ~100k calls/month; confirm in the RDM subscription page), `x-apikey` header carrying the consumer key. Legacy OpenLDBWS tokens no longer work. The gateway returned 403 to Python's default `Python-urllib` User-Agent while curl was accepted, so the adapter sends an explicit User-Agent.

Confirmed route: `https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120/GetDepartureBoard/{CRS}` (`numRows` up to 150; `filterCrs`, `filterType`, `timeOffset`, `timeWindow` default 120 min). Reference material: the RDM OpenAPI spec and `LDBWS_Documentation.pdf` from the product's Documentation tab.

## Architecture

```
RDM LDBWS  <--HTTPS, outbound only--  panel (ESP32-C6, key + Wi-Fi in its NVS)
```

The panel polls LDBWS itself (the direction on screen every minute, the other two every 4 minutes, nothing overnight) and parses the board on the device. See `docs/architecture-decision.md` for why this replaced the original home-side adapter, and for the security trade-off. `adapter/` remains as the mock LDBWS server, the reference normaliser (`normalise.py`, the test oracle for the C parser) and `live_fetch.py` for inspecting real boards from a laptop.

## Station and direction

LDBWS has no direction field. Its `filterCrs` + `filterType=to` returns services that **call at** (or end at) that station after this one, so a direction is expressed as "toward a station": each `[direction.N]` section in `config.ini` has a `label` and a `toward` station code (empty = all trains). The example config is set up for Berkhamsted (BKM):

| Direction (`config.ini`) | LDBWS filter | Header label |
|---|---|---|
| `direction.1`, `toward = HML` (the default) | `to` Hemel Hempstead, the next station towards London | Southbound |
| `direction.2`, `toward = TRI` | `to` Tring, the next station the other way | Northbound |
| `direction.3`, `toward =` | none | All trains |

For a direction of travel, use the **next station along the line** in that direction: every train that way calls there, whatever its destination, so the filter does not depend on a list of terminus stations. Destinations are passed through untouched, so whatever else runs (other terminals, through services) appears under its real name.

Verified live for the example station on 2026-10-02 (one 2-hour window, 10 trains): `HML` returned the London Euston services (platform 4) and `TRI` the northbound services (to Bletchley, Milton Keynes Central and Northampton; platform 3). Together they covered **all 10** unfiltered trains, with no overlap. This works because the line is served by stopping trains. On a line where fast trains skip the next station, that filter would miss them, so pick a station every train stops at and check with `python3 adapter/live_fetch.py --direction 1` (and `--all` to compare). A `toward` station that the trains never call at simply returns an empty board.

- An empty board is a normal, live response: `trainServices` is absent and `areServicesAvailable` stays true. The panel does not treat it as an outage; it has a "No trains" screen.

### Later: filter by a station the train stops at

Probably needs **no second API**. The same service already offers this two ways:

1. The `to` filter itself is a calls-at test for any downstream station, one station at a time.
2. `GetDepBoardWithDetails` (same route family, `numRows` <= 10) adds `subsequentCallingPoints` per service, verified live. Combining criteria (e.g. northbound **and** stops at a particular station) would then be a filter in the panel, with a short `stops` list.

A separate timetable API (e.g. for stations beyond the real-time window, or schedule-only fallback) is only needed if those two fall short; deferred.

## Request budget

The panel keeps a cached board for each swipeable direction: the one on screen is refreshed every 60 s, the other directions every 240 s (both intervals are settings in `config.ini`), and a swipe refreshes its target at once if the cache is more than 10 s old. Steady state is about 1.5 requests a minute while active (~65k a month if it never slept). With quiet hours (21:00-06:00, no polling) it is about 15 active hours a day, so roughly 40k a month, plus a few per swipe or wake. The free tier is **believed** to be ~100k a month, taken from a secondary source; the API sends no rate-limit headers, so confirm the figure on the RDM subscription page. `tools/provision.py` prints a monthly estimate for your settings and warns above 90,000.

## Known limitations that shape the design

1. **"On time" can be schedule-derived.** Darwin may report `On time` with no real-time forecast, and the board cannot say which. The adapter cannot make this distinction; the panel only claims "live" when the fetch itself is fresh.
2. **No planned platform.** LDBWS gives only the current platform. The adapter remembers each `serviceID`'s previous platform to emit `plat_was`; the first poll after restart cannot flag a change.
3. **No schedule-only mode.** When Darwin has no board (`areServicesAvailable: false`) or is unreachable, LDBWS offers nothing to fall back on. The `scheduled`/`TIMETABLE` screen is designed and renders from fixtures, but the live panel does not enter it: with no timetable source it shows the last good board aging to `STALE`. **Backlog, low priority: the value is in live data, and the timetable is already well known.** If revisited: a small static weekday timetable held in the firmware, or a timetable API.
4. **Shapes verified, with quirks.** The real response matches the spec, with these notes: `generatedAt` has 7 fractional digits; `nrccMessages` is `null` when absent and (per spec) a list of `{"Value": "<p>…<a>…</a></p>"}` objects when present, which the adapter strips to plain text; each service also carries undocumented `futureDelay`/`futureCancellation` flags, currently unused. `filterLocationCancelled` (train no longer calls at the filter station) is mapped to `cancelled` with the reason "Not stopping at <station>".

## Mock

`adapter/mock_ldbws.py` serves `testdata/ldbws/*.json` (synthetic, regenerated by `adapter/gen_raw_mocks.py` in the real response shape; invented service IDs and times) on loopback with scenarios `normal|delayed|cancelled|empty|unavailable`; `filterCrs=ZZZ` returns the empty board, as the real service does for a station the trains never call at. No raw live captures are committed. Contract tests: `python3 -m unittest discover adapter`.

## Sources

- https://www.nationalrail.co.uk/developers/darwin-data-feeds/
- https://lite.realtime.nationalrail.co.uk/openldbws/
- RDM LDBWS OpenAPI spec and `LDBWS_Documentation.pdf` (from the RDM product page)
