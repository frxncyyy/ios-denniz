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
executed_rxheader = json.loads((ROOT / "research/nord2/wifi-main-rxheader/ios/manifest.json").read_text())
assert executed_rxheader["image_sha256"] == "0a4b7ade57f35b2cd1932696f3e3bf7ccd10ab0685d7e69b757db0f19996b13b"
assert executed_rxheader["commandline"] == " ".join(future)
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
assert schema["parent_verified"] is True
assert schema["required_new_diagnostic_markers"] == [
    "NORD2 WIFI MAIN PRESPAN:", "NORD2 WIFI MAIN PREORDER:",
    "NORD2 WIFI MAIN DRAIN:", "NORD2 WIFI MAIN TXDESC:",
    "NORD2 WIFI MAIN TXSUBMIT:", "NORD2 WIFI MAIN TXOBSERVED:",
    "NORD2 WIFI MAIN TXWB:", "NORD2 WIFI MAIN RXDESC:",
    "NORD2 WIFI MAIN RXWORDS:", "NORD2 WIFI MAIN RXHEADER:"]
review_path=ROOT / "research/nord2/wifi/next-stage/main-fw-transport/init-event-format/manifest.json"
assert hashlib.sha256(review_path.read_bytes()).hexdigest()=="e174323a5dd5605c3a8ea7569e34ebd29e733fecb5e493de432daa7dbf89901d"
review=json.loads(review_path.read_text())
assert set(review["modified"])=={"wifi_main_transport.c","wifi_main_phy_force.c",
                                "wifi_main_layout.c","wifi_main_wfdma_bind.inc"}
assert review["new_markers"]==[]
for key,value in schema.items():
    if key.endswith("_path"):
        stem=key[:-5]
        assert value and not Path(value).is_absolute() and ".." not in Path(value).parts
        assert hashlib.sha256((ROOT/value).read_bytes()).hexdigest()==schema[stem+"_sha256"]
for name,stem in (("wifi_main_transport.c","rxformat_transport_source"),
                  ("wifi_main_phy_force.c","rxformat_phy_source"),
                  ("wifi_main_layout.c","rxformat_layout_source"),
                  ("wifi_main_wfdma_bind.inc","rxformat_native_finish_source")):
    assert schema[stem+"_sha256"]==review["modified"][name]["candidate"]
assert not any(P.glob("*.img")) and not (P / "payload.bin").exists()
assert not any((P / name).exists() for name in
    ("manifest.json", "image-build.log", "flash.log", "client-hardware.log",
     "hardware-live", "preserved-failed-boot.log", "authorization-used.json"))
assert approval["scope"] == "boot-with-main-wifi-rxformat-calibration"
assert approval["authorized_at"] is None
build = (P / "build-image.sh").read_text()
assert "--enable-nord2wifimain" in build and '"$temp/main.adt"' in build
assert '"$temp/main.adt" "$temp/payload.bin"' in build
assert "nord2-ios-wifi-main-rxformat-battery-stop23.img" in build
verify = (P / "verify-image.py").read_text()
runner = (P / "run-approved-boot.py").read_text()
assert "nord2-ios-wifi-main-rxformat-battery-stop23.img" in verify
assert 'approval["scope"] == "boot-with-main-wifi-rxformat-calibration"' in runner
assert 'approval["image_sha256"] == prepared["image_sha256"] == manifest["image_sha256"]' in runner
assert 'approval["image_sha256"] != "0a4b7ade57f35b2cd1932696f3e3bf7ccd10ab0685d7e69b757db0f19996b13b"' in runner
assert '--authorized-wifi-main-rxformat-calibration-boot' in runner
assert "'pool_map_source'" in verify and 'ml_io_map_unmappable((vm_offset_t)base,(vm_size_t)bytes,VM_WIMG_IO)' in verify
assert "'state_candidate_source'" in verify and "'required_new_diagnostic_markers'" in verify
assert "'drain_source'" in verify and "'NORD2 WIFI MAIN DRAIN:'" in verify
assert all(repr(x) in verify for x in ('txdone_transport_source', 'txdone_transport_header', 'txdone_coordinator_source'))
assert all(repr(x) in verify for x in ("NORD2 WIFI MAIN TXDESC:", "NORD2 WIFI MAIN TXSUBMIT:", "NORD2 WIFI MAIN TXOBSERVED:", "NORD2 WIFI MAIN TXWB:"))
assert all(repr(x) in verify for x in ("txwriteback_transport_source", "txwriteback_transport_header", "txwriteback_coordinator_source"))
assert "NORD2 WIFI MAIN TXWB: constructed=%u observed=%u ddone=%u consumed_zero=%u software_used=%u" in verify
assert all(repr(x) in verify for x in ("rxheader_transport_source", "rxheader_transport_header", "rxheader_coordinator_source"))
assert all(x in verify for x in ('NORD2 WIFI MAIN RXDESC: addr=%#llx buffer=%#llx didx=%u valid=%#x reason=%u', 'NORD2 WIFI MAIN RXWORDS: raw=%#x/%#x/%#x/%#x', 'NORD2 WIFI MAIN RXHEADER: expected=%#x/%u type=%#x event=%#x/%u status=%#x cid=%#x valid=%#x'))
deps = json.loads((P / "parser-dependencies.json").read_text())
for pin in deps["copied_parser_sources"]:
    target, original = ROOT / pin["path"], ROOT / pin["executed_stage_path"]
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    assert digest == pin["sha256"] == pin["executed_stage_sha256"]
    assert hashlib.sha256(original.read_bytes()).hexdigest() == digest
rom_pin = deps["historical_rom_parser"]
assert hashlib.sha256((ROOT / rom_pin["path"]).read_bytes()).hexdigest() == rom_pin["sha256"]
print("PASS: RX format image stage closed, exact e0d3 fallback, distinct image approval")
