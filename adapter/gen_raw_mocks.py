"""Regenerate testdata/ldbws/*.json: synthetic boards shaped like real RDM GetDepartureBoard responses.

Shape notes taken from a live call (not committed): 7-digit fractional seconds in generatedAt,
absent trainServices on an empty board, platform "4" for London-bound at Berkhamsted, null
nrccMessages, extra futureDelay/futureCancellation flags. Times and IDs are invented.
"""
import json
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "testdata" / "ldbws"
GEN = "2026-10-02T15:17:47.6126798+01:00"


def svc(std, etd, plat, sid, length=4, cancelled=False, cancel=None, delay=None, filt_cancelled=False):
    return {
        "origin": [{"locationName": "Milton Keynes Central", "crs": "MKC", "via": None, "futureChangeTo": None, "assocIsCancelled": False}],
        "destination": [{"locationName": "London Euston", "crs": "EUS", "via": None, "futureChangeTo": None, "assocIsCancelled": False}],
        "currentOrigins": None, "currentDestinations": None,
        "std": std, "etd": etd, "platform": plat, "operator": "Great Western Railway", "operatorCode": "GW",
        "isCircularRoute": False, "isCancelled": cancelled, "filterLocationCancelled": filt_cancelled,
        "serviceType": "train", "length": length, "detachFront": False, "isReverseFormation": False,
        "cancelReason": cancel, "delayReason": delay, "serviceID": sid, "adhocAlerts": None,
        "futureCancellation": cancelled, "futureDelay": etd not in ("On time", "Cancelled") and not cancelled,
    }


def board(services=None, msgs=None, avail=True):
    b = {"generatedAt": GEN, "locationName": "Berkhamsted", "crs": "BKM", "filterLocationName": "Hemel Hempstead",
         "filtercrs": "HML", "filterType": "to", "platformAvailable": True, "areServicesAvailable": avail,
         "Xmlns": {"Count": 8}}
    if services is not None:
        b["trainServices"] = services
    if msgs:
        b["nrccMessages"] = [{"Value": m} for m in msgs]
    return b


T = [("15:26", "MOCK0001", 8), ("15:56", "MOCK0002", 4), ("16:30", "MOCK0003", 4), ("16:59", "MOCK0004", 4)]
BOARDS = {
    "normal": board([svc(t, "On time", "4", i, n) for t, i, n in T]),
    "delayed": board([svc("15:26", "15:38", "4", "MOCK0001", 8, delay="a signalling problem"),
                      svc("15:56", "Delayed", "4", "MOCK0002"),
                      svc("16:30", "On time", "4", "MOCK0003"), svc("16:59", "On time", "4", "MOCK0004")]),
    "cancelled": board([svc("15:26", "Cancelled", None, "MOCK0001", 8, cancelled=True, cancel="a shortage of train crew"),
                        svc("15:56", "On time", "3", "MOCK0002"),
                        svc("16:30", "On time", "4", "MOCK0003"), svc("16:59", "On time", "4", "MOCK0004")],
                       msgs=["<p>Mock notice: engineering work this weekend may affect <a href=\"https://example.invalid\">services</a>.</p>"]),
    "empty": board(),  # no trainServices key, areServicesAvailable true
    "unavailable": board(avail=False),
}

if __name__ == "__main__":
    for name, b in BOARDS.items():
        (OUT / f"southbound_{name}.json").write_text(json.dumps(b, indent=2) + "\n")
