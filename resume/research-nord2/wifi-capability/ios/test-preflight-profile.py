#!/usr/bin/env python3
"""Offline policy checks; hardware-profile is existing evidence, not new IO."""
from pathlib import Path
import importlib.util

P = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("profile", P / "preflight-profile.py")
profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile)
saved = (P.parent.parent / "wifi/next-stage/main-fw-transport/wfdma/preflight/v2/hardware-profile.txt").read_text()
cfg, rows = profile.capture(saved)
assert profile.classify(cfg, rows) == {
    "idle": True, "stable": True, "first": (0, 0, 0, 0),
    "reads": 112, "rings": 27, "dma_enabled": False,
    "pool_receipt_proven": False, "fw_ready": False,
    "scan_ready": False, "network_ready": False,
}
assert profile.classify(cfg, [tuple(r[:3]) + (0,) + tuple(r[4:]) for r in rows])["idle"]
for word, value in ((2, 1), (3, 0x201), (3, 0x10200), (4, 1), (5, 1)):
    bad = list(rows)
    row = list(bad[0]); row[word] = value; bad[0] = tuple(row)
    assert not profile.classify(cfg, bad)["idle"]
assert not profile.classify((cfg[0] | 1,) + cfg[1:], rows)["idle"]
assert not profile.classify(cfg[:2] + (cfg[2] | 1,) + cfg[3:], rows)["stable"]
for text in (saved.replace("valid=0xf", "valid=0", 1),
             saved.replace("n=26", "n=25", 1),
             saved.replace("NORD2 WIFI DMA RING: n=26", "NORD2 WIFI DMA RING: n=25", 1),
             saved + next(x for x in saved.splitlines(True) if "DMA RING" in x)):
    try:
        profile.capture(text)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed raw profile accepted")
print("PASS: count 0/observed 512 profile, defined-field refusals, complete raw capture")
