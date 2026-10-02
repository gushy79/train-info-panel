"""Tests for tools/provision.py. Run from the repo root: python3 -m unittest discover tools"""
import configparser
import importlib.util
import io
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("provision", ROOT / "tools" / "provision.py")
prov = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prov)

EXAMPLE = (ROOT / "config.example.ini").read_text()


def filled(text=EXAMPLE, **subs):
    """The example config with placeholder secrets replaced (and any other substitutions applied)."""
    text = text.replace("YOUR_WIFI_NAME", "TestNet").replace("YOUR_WIFI_PASSWORD", "correct-horse-9")
    text = text.replace("YOUR_RAIL_DATA_MARKETPLACE_KEY", "abcDEF123456")
    for old, new in subs.items():
        text = text.replace(old, new)
    return text


def parse(text):
    cp = configparser.ConfigParser(interpolation=None, inline_comment_prefixes=None, delimiters=("=",))
    cp.optionxform = str.lower
    cp.read_string(text)
    return prov.parse_config(cp)


class ConfigTests(unittest.TestCase):
    def test_example_with_secrets_filled_is_valid_and_has_the_documented_defaults(self):
        cfg = parse(filled())
        n, s = cfg["numbers"], cfg["strings"]
        self.assertEqual((s["station"], s["name"]), ("BKM", "Berkhamsted"))
        self.assertEqual(cfg["directions"], [("Southbound", "HML"), ("Northbound", "TRI"), ("All trains", "")])
        self.assertEqual((n["vis_int"], n["bg_int"], n["stale_s"], n["tk_touch_ms"], n["tk_upd_ms"], n["tk_late"]), (60, 240, 600, 5000, 15000, 10))
        self.assertEqual((n["q_on"], n["q_start"], n["q_end"], n["awake_s"]), (1, 21, 6, 120))
        self.assertNotIn("d3_to", s)                       # "all trains" stores no filter

    def test_unedited_example_is_refused_with_a_clear_message(self):
        for placeholder in ("YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD", "YOUR_RAIL_DATA_MARKETPLACE_KEY"):
            text = filled().replace("TestNet", "YOUR_WIFI_NAME") if placeholder == "YOUR_WIFI_NAME" else EXAMPLE
            with self.assertRaises(prov.ConfigError) as e:
                parse(EXAMPLE)
            self.assertIn("placeholder", str(e.exception))

    def test_a_config_with_only_the_required_values_works_on_defaults(self):
        minimal = "[wifi]\nssid = Net\npassword = password1\n[api]\nkey = k123\n[direction.1]\nlabel = Trains\n"
        cfg = parse(minimal)
        self.assertEqual(cfg["strings"]["station"], "BKM")
        self.assertEqual(cfg["numbers"]["vis_int"], 60)
        self.assertEqual(cfg["directions"], [("Trains", "")])

    def test_friendly_input_is_normalised(self):
        cfg = parse(filled(**{"crs = BKM": "crs = bkm", "toward = HML": "toward = hml"}))
        self.assertEqual(cfg["strings"]["station"], "BKM")
        self.assertEqual(cfg["strings"]["d1_to"], "HML")
        cfg = parse(filled(**{"enabled = yes": "enabled = No", "alert_seconds_after_touch = 5": "alert_seconds_after_touch = 2.5"}))
        self.assertEqual((cfg["numbers"]["q_on"], cfg["numbers"]["tk_touch_ms"]), (0, 2500))

    def test_mistakes_get_specific_errors(self):
        cases = {
            "station code": filled(**{"crs = BKM": "crs = BERKHAMSTED"}),
            "between 30 and 3600": filled(**{"visible_interval_seconds = 60": "visible_interval_seconds = 5"}),
            "must be a number": filled(**{"swipe_time_ms = 900": "swipe_time_ms = fast"}),
            "yes or no": filled(**{"enabled = yes": "enabled = maybe"}),
            "8 to 64": filled(**{"correct-horse-9": "short"}),
            "without gaps": filled(**{"[direction.2]": "[direction.4]"}),
            "itself": filled(**{"toward = HML": "toward = BKM"}),
            "plain text": filled(**{"label = Southbound": "label = Café"}),
            "too long": filled(**{"label = Southbound": "label = " + "x" * 17}),
            "at least one direction": "[wifi]\nssid=N\npassword=password1\n[api]\nkey=k\n",
            "not be longer": filled(**{"show_age_after_seconds = 120": "show_age_after_seconds = 3000"}),
            "between 0 and 60": filled(**{"alert_seconds_on_update = 15": "alert_seconds_on_update = 90"}),
            "ost 5 directions": filled() + "\n[direction.4]\nlabel=a\n[direction.5]\nlabel=b\n[direction.6]\nlabel=c\n",
        }
        for expect, text in cases.items():
            with self.assertRaises(prov.ConfigError, msg=expect) as e:
                parse(text)
            self.assertIn(expect, str(e.exception), msg=expect)

    def test_special_characters_in_secrets_survive(self):
        pw = "p#ss%word;with=chars 1"
        cfg = parse(filled(**{"correct-horse-9": pw}))
        self.assertEqual(cfg["strings"]["wifi_pass"], pw)   # no comment stripping, no % interpolation

    def test_request_estimate(self):
        n = parse(filled())["numbers"]
        est = prov.monthly_requests(n, 3)
        self.assertTrue(30_000 < est < 50_000, est)                 # defaults: ~40k a month
        n["q_on"] = 0
        self.assertGreater(prov.monthly_requests(n, 3), est)       # no quiet hours: more
        n["vis_int"] = 30; n["bg_int"] = 60
        self.assertGreater(prov.monthly_requests(n, 5), prov.QUOTA_WARNING)   # aggressive: over the free tier


class FirmwareAgreementTests(unittest.TestCase):
    """The tool and the firmware must agree on key names and value ranges, or a setting is silently lost."""

    def test_every_key_the_tool_writes_is_read_by_the_firmware_and_vice_versa(self):
        written = set(parse(filled())["strings"]) | set(parse(filled())["numbers"])
        written = {re.sub(r"^d\d_", "dN_", k) for k in written} | {"dN_to"}
        src = (ROOT / "firmware/main/net/settings_nvs.c").read_text() + (ROOT / "firmware/main/net/poller.c").read_text()
        read = set(re.findall(r'(?:get_str|get_u32|get_u8|get_u16|nvs_str)\(h, "(\w+)"', src))
        read |= {"dN_label", "dN_to"} if 'd%d_label' in src and 'd%d_to' in src else set()
        read -= {"direction"}   # the panel's own saved selection: UI state in the separate "state" namespace, not config
        self.assertEqual(written, read, f"only written: {written - read}; only read: {read - written}")

    def test_value_ranges_match_the_firmware_clamps(self):
        field_to_key = {
            "visible_interval_s": "vis_int", "background_interval_s": "bg_int", "request_timeout_s": "req_to",
            "num_rows": "rows", "stale_after_s": "stale_s", "age_shown_after_s": "age_s",
            "alert_touch_ms": "tk_touch_ms", "alert_update_ms": "tk_upd_ms",
            "takeover_min_late_min": "tk_late", "quiet_start_hour": "q_start", "quiet_end_hour": "q_end",
            "awake_after_touch_s": "awake_s", "swipe_min_px": "swp_px", "swipe_max_ms": "swp_ms",
        }
        src = (ROOT / "firmware/main/board/settings.c").read_text()
        firmware = {}
        for field, lo, hi in re.findall(r"clamp_u\d+\(&s->(\w+), (\d+), (\d+)\)", src):
            firmware[field_to_key[field]] = (int(lo), int(hi))
        tool = {k: (lo * scale, hi * scale) for _, _, _, _, lo, hi, k, scale in prov.NUMBERS}
        for key, rng in firmware.items():
            if key == "age_s":
                continue   # the tool's lower bound for age is its own (30) and its upper bound is the stale limit
            self.assertEqual(tool[key], rng, key)
        self.assertEqual(set(firmware) - {"age_s"}, set(tool) - {"age_s"})

    def test_defaults_in_the_example_match_the_firmware_defaults(self):
        c = (ROOT / "firmware/main/board/settings.c").read_text()
        n = parse(filled())["numbers"]
        expect = {"vis_int": "visible_interval_s", "bg_int": "background_interval_s", "req_to": "request_timeout_s",
                  "awake_s": "awake_after_touch_s", "q_start": "quiet_start_hour", "q_end": "quiet_end_hour",
                  "swp_px": "swipe_min_px", "swp_ms": "swipe_max_ms", "tk_late": "takeover_min_late_min"}
        for key, field in expect.items():
            m = re.search(rf"s->{field} = (\d+);", c)
            self.assertEqual(int(m[1]), n[key], key)
        self.assertEqual(int(re.search(r"s->alert_touch_ms = (\d+);", c)[1]), n["tk_touch_ms"])
        self.assertEqual(int(re.search(r"s->alert_update_ms = (\d+);", c)[1]), n["tk_upd_ms"])
        self.assertEqual(int(re.search(r"s->stale_after_s = (\d+);", c)[1]), n["stale_s"])


class FirmwareImageTests(unittest.TestCase):
    def check(self, blob):
        with tempfile.NamedTemporaryFile(suffix=".bin") as f:
            f.write(blob); f.flush()
            try:
                prov.check_firmware_image(f.name)
                return True
            except SystemExit:
                return False

    def test_only_a_merged_image_is_accepted(self):
        merged = bytearray(b"\xff" * 0x9000); merged[0] = 0xE9; merged[0x8000:0x8002] = b"\xaa\x50"
        self.assertTrue(self.check(bytes(merged)))
        bare_app = bytearray(b"\x00" * 0x9000); bare_app[0] = 0xE9           # an app/bootloader image alone
        self.assertFalse(self.check(bytes(bare_app)))                         # the mistake that bricks a board
        self.assertFalse(self.check(b"\xE9" + b"\x00" * 100))                 # too short to be merged
        self.assertFalse(self.check(b"not firmware at all" * 3000))


class SafetyTests(unittest.TestCase):
    def run_tool(self, text, *args):
        with tempfile.TemporaryDirectory() as d:
            cfg = Path(d) / "config.ini"
            cfg.write_text(text)
            return subprocess.run([sys.executable, str(ROOT / "tools/provision.py"), "--config", str(cfg), *args],
                                  capture_output=True, text=True)

    def test_secrets_are_never_printed_and_temp_files_are_removed(self):
        before = set(Path(tempfile.gettempdir()).glob("panel-nvs-*"))
        r = self.run_tool(filled(**{"correct-horse-9": "Sup3r-Secret-pw"}), "--dry-run")
        out = r.stdout + r.stderr
        for secret in ("Sup3r-Secret-pw", "abcDEF123456"):
            self.assertNotIn(secret, out)
        self.assertEqual(set(Path(tempfile.gettempdir()).glob("panel-nvs-*")), before)

    def test_bad_config_exits_nonzero_with_a_message_and_flashes_nothing(self):
        r = self.run_tool(EXAMPLE, "--dry-run")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("config problem", r.stdout + r.stderr)

    def test_missing_config_points_at_the_example(self):
        r = subprocess.run([sys.executable, str(ROOT / "tools/provision.py"), "--config", "/nonexistent/config.ini"], capture_output=True, text=True)
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("config.example.ini", r.stdout + r.stderr)

    def test_dry_run_builds_a_real_settings_image_when_the_helper_is_installed(self):
        r = self.run_tool(filled(), "--dry-run")
        out = r.stdout + r.stderr
        if "missing a helper tool" in out:
            self.skipTest("esp-idf-nvs-partition-gen not installed here")
        self.assertEqual(r.returncode, 0, out)
        self.assertIn("settings image built", out)


if __name__ == "__main__":
    unittest.main()
