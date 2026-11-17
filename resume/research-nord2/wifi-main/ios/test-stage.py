#!/usr/bin/env python3
"""Static, offline checks of a deliberately unprepared future image stage."""
from pathlib import Path
import json
import hashlib
import struct

P = Path(__file__).resolve().parent
ROOT = P.parents[3]
baseline = json.loads((P / "baseline.json").read_text())
prior = json.loads((ROOT / "research/nord2/wifi-dma/ios/manifest.json").read_text())
assert prior["image_sha256"] == baseline["fallback_image_sha256"]
assert prior["image_bytes"] == baseline["fallback_image_bytes"]
old_payload = (ROOT / "research/nord2/wifi-dma/ios/payload.bin").read_bytes()
adt_off, adt_size = struct.unpack_from("<QQ", old_payload, 0x20)
assert adt_off + adt_size <= len(old_payload)
assert adt_size == baseline["fallback_payload_adt_bytes"]
assert hashlib.sha256(old_payload[adt_off:adt_off + adt_size]).hexdigest() == baseline["fallback_payload_adt_sha256"]
tokens = prior["commandline"].split()
assert all(x in tokens for x in baseline["existing_required_boot_args"])
assert not any(x.startswith("nord2wifimain=") for x in tokens)
future = tokens + [baseline["future_boot_arg"]]
keys = [x.split("=", 1)[0] for x in future if "=" in x]
assert len(keys) == len(set(keys)) and len(" ".join(future).encode()) < 608
assert baseline["network_ready_claim"] is False and baseline["scan_ready_claim"] is False
identity = json.loads((P / "identity-required.json").read_text())
prepared = json.loads((P / "prepared-image-required.json").read_text())
approval = json.loads((P / "authorization-required.json").read_text())
schema = json.loads((P / "native-schema-required.json").read_text())
assert identity["parent_verified"] is False and all(identity[k] is None for k in
    ("ci_run", "source_commit", "kernel_path", "kernel_sha256", "ci_run_json"))
assert prepared["parent_verified"] is False and all(prepared[k] is None for k in
    ("image_sha256", "image_bytes", "image_path"))
assert approval["human_exact_image_authorized"] is False
assert approval["rf_calibration_authorized"] is False
assert approval["conditional_recovery_authorized"] is False
assert approval["image_sha256"] is None and approval["device_serial"] is None
assert schema["parent_verified"] is False
assert all(schema[k] is None for k in
    ("wfdma_native_path", "wfdma_native_sha256", "main_native_v2_path",
     "main_native_v2_sha256", "preflight_v2_source_path",
     "preflight_v2_source_sha256", "battery_fifo_low5_source_path",
     "battery_fifo_low5_source_sha256", "evidence_parser_path",
     "evidence_parser_sha256"))
assert not any(P.glob("*.img")) and not (P / "payload.bin").exists()
build = (P / "build-image.sh").read_text()
assert "--enable-nord2wifimain" in build and '"$temp/main.adt"' in build
assert '"$temp/main.adt" "$temp/payload.bin"' in build
assert "nord2-ios-wifi-main-battery-stop23.img" in build
print("PASS: new image stage gates closed, exact e0d3 fallback, distinct main flag")
