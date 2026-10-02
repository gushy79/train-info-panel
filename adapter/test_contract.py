"""Host-side contract test: python3 -m unittest discover adapter  (from the repo root)."""
import json
import re
import sys
import threading
import unittest
import urllib.error
import urllib.request
from http.server import HTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_fixtures
import mock_ldbws
from normalise import MAX_SERVICES, Unavailable, normalise

PAYLOADS = gen_fixtures.ROOT / "payload"
HHMM = re.compile(r"^([01]\d|2[0-3]):[0-5]\d$")
STATES = {"on_time", "delayed", "cancelled", "unknown"}


def validate(p):
    """Mirror of the rules in docs/payload.md; the firmware parser must enforce the same."""
    assert p["v"] == 1
    assert p["mode"] in ("live", "scheduled")
    assert isinstance(p["generated"], int) and p["generated"] > 0
    assert len(p["station"]["crs"]) == 3 and p["station"]["name"]
    f = p["filter"]
    assert f is None or (f["mode"] in ("to", "from") and len(f["crs"]) == 3 and f["name"] and 0 < len(f["label"]) <= 16)
    assert p["message"] is None or len(p["message"]) <= 80
    assert len(p["services"]) <= MAX_SERVICES
    for s in p["services"]:
        assert HHMM.match(s["std"])
        assert s["state"] in STATES
        assert s["etd"] is None or HHMM.match(s["etd"])
        assert s["late"] >= 0
        assert s["dest"]
        assert s["why"] is None or len(s["why"]) <= 60
        if s["state"] == "cancelled":
            assert s["etd"] is None
        if p["mode"] == "scheduled":
            # The no-false-live rule: scheduled data carries no realtime claims.
            assert s["state"] == "unknown" and s["etd"] is None and s["plat"] is None


class FixtureTests(unittest.TestCase):
    def test_committed_fixtures_match_generator(self):
        for name, payload in gen_fixtures.build().items():
            committed = json.loads((PAYLOADS / f"{name}.json").read_text())
            self.assertEqual(committed, payload, name)

    def test_all_fixtures_valid(self):
        for name in gen_fixtures.build():
            validate(json.loads((PAYLOADS / f"{name}.json").read_text()))

    def test_malformed_is_rejected(self):
        with self.assertRaises(json.JSONDecodeError):
            json.loads((PAYLOADS / "malformed.json").read_text())

    def test_delay_cancel_platform_semantics(self):
        d = json.loads((PAYLOADS / "delayed.json").read_text())["services"]
        self.assertEqual((d[0]["state"], d[0]["late"], d[0]["etd"]), ("delayed", 12, "15:38"))
        self.assertEqual((d[1]["state"], d[1]["etd"]), ("delayed", None))  # "Delayed" with no time
        c = json.loads((PAYLOADS / "cancelled_platform_change.json").read_text())["services"]
        self.assertEqual((c[0]["state"], c[0]["etd"], c[0]["plat"]), ("cancelled", None, None))
        self.assertEqual((c[1]["plat"], c[1]["plat_was"]), ("3", "4"))
        self.assertIsNone(c[2]["plat_was"])

    def test_empty_board_is_live_with_no_services(self):
        p = json.loads((PAYLOADS / "empty.json").read_text())
        self.assertEqual((p["mode"], p["services"]), ("live", []))

    def test_nrcc_message_objects_are_flattened(self):
        c = json.loads((PAYLOADS / "cancelled_platform_change.json").read_text())
        self.assertTrue(c["message"].startswith("Mock notice:"))
        self.assertNotIn("<", c["message"])
        self.assertNotIn("href", c["message"])

    def test_seven_digit_fractional_seconds_parse(self):
        p = json.loads((PAYLOADS / "live_normal.json").read_text())
        self.assertEqual(p["generated"], 1790950667)  # 2026-10-02T14:17:47Z

    def test_filter_location_cancelled_reads_as_cancelled(self):
        raw = json.loads((gen_fixtures.ROOT / "ldbws" / "southbound_normal.json").read_text())
        raw["trainServices"][0]["filterLocationCancelled"] = True
        s = normalise(raw, gen_fixtures.FLT)["services"][0]
        self.assertEqual((s["state"], s["why"]), ("cancelled", "Not stopping at Hemel Hempstead"))

    def test_unavailable_board_is_not_turned_into_live(self):
        raw = json.loads((gen_fixtures.ROOT / "ldbws" / "southbound_unavailable.json").read_text())
        with self.assertRaises(Unavailable):
            normalise(raw)

    def test_long_reason_clipped_and_midnight_wrap(self):
        raw = json.loads((gen_fixtures.ROOT / "ldbws" / "southbound_normal.json").read_text())
        raw["trainServices"] = [dict(raw["trainServices"][0], std="23:55", etd="00:03", delayReason="x" * 200)]
        s = normalise(raw)["services"][0]
        self.assertEqual((s["late"], len(s["why"])), (8, 60))


class MockServerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.srv = HTTPServer(("127.0.0.1", 0), mock_ldbws.Handler)
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()
        cls.base = f"http://127.0.0.1:{cls.srv.server_port}{mock_ldbws.ROUTE}BKM"

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown()

    def get(self, query="", key="k"):
        req = urllib.request.Request(self.base + query, headers={"x-apikey": key} if key else {})
        return urllib.request.urlopen(req)

    def test_serves_board_through_normaliser(self):
        board = json.load(self.get("?scenario=delayed"))
        validate(normalise(board, gen_fixtures.FLT))

    def test_oxford_filter_gives_empty_board(self):
        board = json.load(self.get("?filterCrs=ZZZ&filterType=to"))
        self.assertEqual(normalise(board)["services"], [])

    def test_requires_key_and_known_scenario(self):
        with self.assertRaises(urllib.error.HTTPError) as e:
            self.get(key=None)
        self.assertEqual(e.exception.code, 401)
        with self.assertRaises(urllib.error.HTTPError) as e:
            self.get("?scenario=../../etc/passwd")
        self.assertEqual(e.exception.code, 400)


if __name__ == "__main__":
    unittest.main()
