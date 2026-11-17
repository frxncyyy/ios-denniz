#!/usr/bin/env python3
"""Offline byte proof: future ADT differs from executed e0d3 only by WIFI."""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import sys

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import adt  # noqa: E402

baseline = json.loads((P / "baseline.json").read_text())
payload = (ROOT / "research/nord2/wifi-dma/ios/payload.bin").read_bytes()
offset, size = struct.unpack_from("<QQ", payload, 0x20)
assert offset + size <= len(payload)
old = payload[offset:offset + size]
assert len(old) == baseline["fallback_payload_adt_bytes"]
assert hashlib.sha256(old).hexdigest() == baseline["fallback_payload_adt_sha256"]
wifi = (ROOT / "research/nord2/wifi/calibration/WIFI").read_bytes()
spec = importlib.util.spec_from_file_location("inject", ROOT / "research/nord2/wifi/next-stage/main-fw-transport/main-fw-native-v2/inject_cal_adt.py")
inject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inject)
root, end = adt.parse(old)
assert end == len(old)
target = adt.serialize(inject.add(root, wifi))
parsed, end = adt.parse(target)
assert end == len(target)
chosen = [node for node in parsed.children if node.name == "chosen"]
assert len(chosen) == 1
calibration = [value for key, _, value in chosen[0].props if key == "nord2-wifi-calibration"]
assert calibration == [wifi]
chosen[0].props = [entry for entry in chosen[0].props if entry[0] != "nord2-wifi-calibration"]
assert adt.serialize(parsed) == old
print("PASS: sole SHA-pinned private WIFI property; every executed-baseline ADT byte retained")
