# Design decision: board layout

**Chosen: Concept C, two-line rows.** Recorded 2026-10-02.

Renders at exact 320×172, 1:1, for every fixture state: `docs/design/concepts.html`, generated from `testdata/payload/` by `docs/design/build_concepts.py`. Browser fonts (Liberation Sans) stand in for the device font; **all widths are indicative until checked on the physical panel.**

## The three concepts

- **A, aligned board.** Four one-line rows: time 22 px, destination 18 px, platform, status. Header strip with route and freshness chip; footer line for the reason.
- **B, hero + list.** Next train at 52 px time on the left, three followers on the right, footer reason.
- **C, two-line rows.** Three rows. Line one: time 24 px bold + destination 21 px. Line two (13 px): status · platform · reason. Footer (12 px): "Then 09:02 London Euston".

## Assessment

| Criterion | A | B | C |
|---|---|---|---|
| 1. Time-to-answer at arm's length | Good: 4 rows, status colour per row, but 22 px time is no larger than C | **Best**: 52 px next time | Good: 24 px time, 21 px destination, status directly beneath |
| 2. Text sizing / truncation | Destination ~132 px ≈ 14 chars at 18 px; ellipsis-truncated | Hero 146 px, followers 94 px (≈ 11 chars) | Destination 238 px ≈ 20 chars at 21 px; ellipsis only beyond that |
| 3. Long destinations honestly | **Poor**: long terminus names such as "London Waterloo East" (20 chars) truncate on every row | Poor: truncates in the followers, and the hero for anything > ~14 chars | **Good**: a 17-20 character name fits in full; ellipsis (never silent abbreviation) for the rare longer name |
| 4. Colour semantics | Same set in all three (below) | same | same |
| 5. Bad-data day | Footer carries the warning; rows keep times | Hero goes grey and says "Not live" or "Timetable"; followers stay legible | Row status line says it; chip and footer repeat it |

C wins on criterion 3, which matters because the most common destination, a long London terminus name, is also the one that would be truncated in A and B. B is the fastest to read, but it shows the next train only at the cost of cramped followers and large unused area when the next train is routine. A is the densest and most board-like, but its destination column is too narrow for this station. C gives up a fourth row (three trains plus a "Then…" line) to keep the destination whole and give the delay or cancellation reason a place on the row itself.

## Colour and state semantics (all concepts)

Colour is never the only cue; each state also changes text or shape.

| State | Treatment |
|---|---|
| Live | Green chip reading `LIVE` (adds age once ≥ 2 min: `LIVE 3m`). Status text: `On time` (green) |
| Delayed | Amber status `Exp 08:21`, or `Delayed` when no time is given. Reason in the footer. |
| Cancelled | Red, time and destination struck through, status line shows `Cancelled · reason` |
| Platform change | Platform in amber bold with `(was 1)` |
| Scheduled fallback | Grey outlined `TIMETABLE` chip; every row says `Timetable`; no platforms, no realtime claims (enforced by the payload validator) |
| Stale (> 10 min) | Whole screen desaturated, amber `STALE 25m` chip, rows say `Not live`, footer "No live update: times may be wrong" |

Stale and scheduled rows never show `On time` or countdowns. This is the "no false-live" rule from the brief.

## Real-data check (2026-10-02)

Live data confirmed the choice: on a stopping-train line every service in one direction often has the same long destination (for example "London Waterloo East", 20 characters). In A that truncates on every row, in B in every follower; only C shows it whole.

## First hardware check (2026-10-02) and fixes

Photographed on the real panel. Found and fixed:

- **Glyphs.** The built-in Montserrat has no U+00B7 (middle dot) or U+2026 (ellipsis); both rendered as boxes. All on-device text is now ASCII (separator " - "), enforced by a host test that scans every generated row/footer string. The design page uses the same separator.
- **Freshness chip bug.** The chip was sized from an unlaid-out label (a huge placeholder width), producing a light sliver at the right of coloured bars, a full-width amber stale header, and no visible LIVE/TIMETABLE chip. It is now sized from font metrics.
- **Row spacing.** The 22 px destination collided with the 14 px detail line. Destination is now 20 px with the detail line at +26 and the rule at +43.
- **The "Timetable" screen** is the scheduled-fallback state ("live data unavailable"). It was unclear because its chip was missing and the footer had been replaced by "Then ...". The footer now warns on bad data: "Timetable only: live times unavailable" (timetable) or "No live update: times may be wrong" (stale), replacing "Then ..." for those states.
- A changed platform now shows amber ("On time - Platform 3 (was 4)"), not routine green.

## Swipe to change direction (built, verified on the panel)

Swipe left for the next direction, right for the previous (by default Southbound, Northbound, All trains), wrapping. Three dots in the header (left of the freshness chip) show which is selected. Each direction keeps its own cached board, refreshed in the background, so a swipe is instant; the chip shows the cache's true age (`LIVE 3m`) until the refresh lands a second or two later. Only the first-ever visit to a direction shows "Loading...". A direction's board is never shown under another's label. The choice survives a reboot. Gesture: >= 70 px, mostly horizontal, within 0.9 s, once per touch (`firmware/main/board/swipe.c`).

## Quiet hours and screen-off (built, verified on the panel)

Overnight nobody is catching a train, so from 21:00 to 06:00 UK time (follows GMT/BST; all of it set in `config.ini`) the panel makes no requests and turns the screen off: backlight to 0 and the panel's display-off command, still on USB power with Wi-Fi up. A touch wakes it, forces a refresh of the direction on screen, and holds it awake for 2 minutes after the last touch. Entering quiet hours drops the cached boards, so waking never shows yesterday's trains: it draws "Loading..." first, lights the screen a tick later (no flash of old content), then the live board. Until the clock has been set from the network the panel acts as if it is daytime, so a fresh boot never leaves it dark. Logic: `firmware/main/board/quiet_hours.c` (host-tested, incl. midnight wrap and tick-counter wrap).

## Takeover screen (designed)

Render: `docs/design/concepts.html`, bottom section. Full-width coloured band (24 px bold label), the key fact at 60 px, destination at 22 px, a 14 px detail line, and a footer with the following service.

| Trigger (next service only) | Band | Big text | Detail |
|---|---|---|---|
| Cancelled | red, `CANCELLED` | struck-through scheduled time | reason, or "No reason given" |
| Platform changed | amber, `PLATFORM CHANGE` | new platform, `P3` | "Was platform 4 · 15:26 departure" |
| ≥ 10 min late | amber, `DELAYED 12 MIN` | new estimated time | "Was 15:26 · reason" |

Rules (reference implementation: `takeoverKind()` in the concept page; to be ported to firmware with a host test):

- **Only the next service** (`services[0]`) can trigger one. Priority: cancelled, then platform change, then delay.
- **Only fresh live data**: never for `scheduled` mode or stale data. A takeover is a strong claim, and it must not be made from data the panel can't vouch for.
- **Two lengths, because there are two situations** (both settings in `config.ini`). *After a touch* (you swiped or tapped in the last 10 s, so you are looking at the panel): **5 s**. It only has to register, and it stands between you and the times you came for. *On a background update* (the refresh found it and you may be across the room): **15 s**, long enough to be noticed. The length is fixed when the alert appears. Either can be set to 0 to switch that kind off. It re-arms only when that service's trigger signature changes (kind, `etd` or platform), so each poll doesn't re-trigger it. (History: 15 s was judged too long when swiping, then 8 s, then 5 s; the split came from the observation that a refresh landing while nobody is looking needs the longer time.)
- **Delay shown as "Delayed" with no time**, and delays under 10 min, stay in the board (amber), no takeover: the size of the delay is unknown or routine.
- Footer shows the next *running* service (skipping cancelled ones), with its platform and any delay.
- Band colour is paired with a text label; the `LIVE` chip stays visible, with age once ≥ 2 min.
- Optional touch: a tap could dismiss early. Not required for v1.

## Empty live board (designed)

"No trains / <direction label> / None expected in the next 2 hours", with the header and the `LIVE` chip so it reads as a real answer rather than a failure. Needed because LDBWS returns an empty list in normal operation (for example when nothing is due in the next two hours, or the filter station is not served at that hour).

## Open items

- **How long a cancelled row stays.** LDBWS decides when a departed or cancelled service leaves the board; the panel follows it. Watch this during the soak test before inventing a device-side rule.
- **Timetable fallback screen** is designed but not reachable on the live panel (no timetable source; backlog, `docs/data-source.md`).
- **Fonts.** The panel uses regular-weight Montserrat; the design's bold weights and 60 px takeover figure are approximated (48 px). Judged acceptable on the real panel.
- Resolved on hardware: header label wording, row spacing, takeover length (5 s), chip legibility, and the cancelled, delayed, platform-change, stale and timetable renders were all photographed and accepted. The empty-board screen and the cancelled takeover have been checked in renders and logs but not photographed.

## Minimum sizes (brief compliance)

Nothing on the panel is smaller than 12 px: the smallest text is the 12 px footer and chip labels; row status lines are 14 px.
