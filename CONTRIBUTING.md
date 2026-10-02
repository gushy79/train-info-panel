# Contributing

This file is for people **changing** the project. If you only want to use a panel, the [README](README.md) is
all you need.

## The 60-second tour

A Waveshare ESP32-C6-Touch-LCD-1.47 polls National Rail's Live Departure Board Web Service (LDBWS, via Rail Data
Marketplace) directly, parses it on the device, and draws it with LVGL. All behaviour values come from the
device's flash, where `tools/provision.py` writes them from the user's `config.ini`. Nothing user-adjustable is
compiled in. The panel opens no inbound connections.

```text
config.example.ini  every user setting, documented: copy to config.ini
tools/              provision.py (settings + firmware -> panel), package_release.py, requirements.txt
firmware/           ESP-IDF + LVGL firmware (see firmware/README.md for the code map)
adapter/            Python reference implementation and mock service: the firmware's test oracle
testdata/           synthetic fixtures only (never commit real captured responses)
docs/               design and architecture decisions, hardware notes, rendered design reference
```

## Set up

Building from source (ESP-IDF install, `idf.py`, `tools/package_release.py`) is described in the README's
[Build it yourself](README.md#build-it-yourself). The code map, host tests and recovery steps are in
[`firmware/README.md`](firmware/README.md).

## Tests (run all three before a change)

```sh
python3 -m unittest discover adapter     # Python reference normaliser, mock service, fixtures
python3 -m unittest discover tools       # the config tool, incl. checks that it agrees with the firmware
firmware/test/host/run.sh                # the C logic under ASan/UBSan, no hardware needed
```

The C parser and the Python reference are fed the same raw boards and must agree field for field. The tool
tests fail if `provision.py` and the firmware disagree about a setting's key name, range or default.

## Adding or changing a setting

A setting touches five places, and the tests catch most mistakes:

1. `firmware/main/board/settings.h` / `settings.c`: the field, its default, and its clamp range.
2. `firmware/main/net/settings_nvs.c`: read it from NVS (keys are 15 characters at most).
3. `tools/provision.py`: add it to `NUMBERS` (or the string handling) with the same range.
4. `config.example.ini`: document it, with the default.
5. Use `settings()->field` in the logic. Logic that must be right lives in `firmware/main/board/` with no
   ESP-IDF dependency, so it is host-tested; add a test in `firmware/test/host/`.

## Conventions

- **No secrets, ever.** `config.ini` is git-ignored. Check what you are about to commit; never put real keys,
  Wi-Fi details, captured API responses or personal data in code, docs, fixtures or commit messages.
- **Honesty over polish.** Stale or timetable data must never look live; that rule is tested.
- **The panel font is ASCII only**: the built-in Montserrat has no middle dot, ellipsis or en dash (they render
  as boxes). A host test enforces it for generated strings.
- Fixtures are generated: change `adapter/gen_raw_mocks.py` / `adapter/gen_fixtures.py`, then run
  `python3 adapter/gen_raw_mocks.py && python3 adapter/gen_fixtures.py && python3 firmware/tools/gen_fixtures_c.py`.
- The design reference page is generated too: `python3 docs/design/build_concepts.py`, then open
  `docs/design/concepts.html`.

## Documentation map

| Doc | What it records |
|---|---|
| [`docs/data-source.md`](docs/data-source.md) | Why LDBWS via Rail Data Marketplace; how directions work; request budget; API quirks |
| [`docs/architecture-decision.md`](docs/architecture-decision.md) | The panel talks to the service directly; secrets and settings in its flash; trade-offs |
| [`docs/design-decision.md`](docs/design-decision.md) | The layout chosen and why, colour meanings, alerts, swipe, quiet hours |
| [`docs/payload.md`](docs/payload.md) | The compact board model shared by fixtures, the reference code and the firmware |
| [`docs/hardware-notes.md`](docs/hardware-notes.md) | Findings about the board (touch controller, fonts) |
| [`docs/firmware-toolchain.md`](docs/firmware-toolchain.md) | ESP-IDF + LVGL, and why |
| [`docs/design/concepts.html`](docs/design/concepts.html) | Every screen rendered at 1:1 (open in a browser) |
| [`docs/status/current/project-status.md`](docs/status/current/project-status.md) | Where things stand and the backlog |
| [`CLAUDE.md`](CLAUDE.md) | The original product and engineering brief |

By contributing you agree your work is licensed under the project's Apache License 2.0.
