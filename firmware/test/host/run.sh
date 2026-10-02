#!/bin/sh
# Host-side tests of firmware/main/board (no hardware, no ESP-IDF build): presentation rules, the
# LDBWS parser (cross-checked against the Python adapter's fixtures), backoff, takeover timing.
# Needs gcc and a checkout of ESP-IDF only for its bundled cJSON (default ~/esp/esp-idf).
set -e
here=$(cd "$(dirname "$0")" && pwd)
src="$here/../../main"
cjson="${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON"
[ -f "$cjson/cJSON.c" ] || { echo "cJSON not found at $cjson (set IDF_PATH)"; exit 2; }
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
cflags="-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined -g"

gcc $cflags -I"$src" "$src/board/board_logic.c" "$src/board/settings.c" "$src/fixtures/fixtures.c" "$here/test_board_logic.c" -o "$out/board_logic"
"$out/board_logic"

# cJSON is third-party code: compiled without -Werror so its own warnings do not gate us.
gcc -std=c11 -fsanitize=address,undefined -g -c "$cjson/cJSON.c" -o "$out/cJSON.o"
gcc $cflags -DTESTDATA_DIR="\"$here/../../../testdata/ldbws\"" -I"$src" -I"$cjson" \
    "$src/board/board_logic.c" "$src/board/ldbws_parse.c" "$src/board/backoff.c" "$src/board/takeover_tracker.c" "$src/board/swipe.c" "$src/board/schedule.c" "$src/board/quiet_hours.c" "$src/board/settings.c" \
    "$src/fixtures/fixtures.c" "$here/test_net_logic.c" "$out/cJSON.o" -o "$out/net_logic"
"$out/net_logic"
