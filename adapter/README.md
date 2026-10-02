# adapter/

**Not part of the running system.** The panel fetches and parses LDBWS itself (`docs/architecture-decision.md`).
This directory is the Python reference implementation and tooling around it, kept because it is useful and
because it is the oracle the firmware's parser is tested against.

| File | Purpose |
|---|---|
| `normalise.py` | LDBWS station board -> the compact board model (`docs/payload.md`). The readable reference for `firmware/main/board/ldbws_parse.c`. |
| `mock_ldbws.py` | Loopback stand-in for the real endpoint serving `testdata/ldbws/*.json` (scenarios `normal`, `delayed`, `cancelled`, `empty`, `unavailable`). |
| `gen_raw_mocks.py`, `gen_fixtures.py` | Regenerate `testdata/ldbws` (synthetic raw boards) and `testdata/payload` (normalised) deterministically. |
| `live_fetch.py` | Fetch one real board and print the model, using the station, directions and key from `config.ini`: `python3 adapter/live_fetch.py --direction 2`, or `--toward EUS` to try any station code. Never prints the key. |
| `test_contract.py` | Contract tests: fixtures match the generator, semantics (delay, cancellation, platform change, empty board), the mock server. |

Run: `python3 -m unittest discover adapter` from the repo root. Pure Python 3 standard library.

The firmware's host test (`firmware/test/host/run.sh`) feeds the same raw boards to the C parser and requires
identical results, so a change to either implementation that breaks the other fails a test.
