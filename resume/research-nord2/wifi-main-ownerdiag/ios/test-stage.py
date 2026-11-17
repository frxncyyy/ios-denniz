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
executed_rxformat = json.loads((ROOT / "research/nord2/wifi-main-rxformat/ios/manifest.json").read_text())
assert executed_rxformat["image_sha256"] == "8f1824f18a3ec8a70d75dbbcc65a8f2075da2fe12259253c1a0896786e9bcf0b"
assert executed_rxformat["commandline"] == " ".join(future)
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
    "NORD2 WIFI MAIN RXWORDS:", "NORD2 WIFI MAIN RXHEADER:",
    "NORD2 WIFI MAIN GUARD:"]
assert schema["ownerdiag_required_markers"] == ["NORD2 WIFI MAIN GUARD:"]
assert schema["ownerdiag_candidate_manifest_sha256"] == "9a8b2a3f5ea81613bf1825ddf830ff7c2816da79883425075e688b25cf88c6a0"
assert hashlib.sha256((ROOT / schema["ownerdiag_candidate_manifest_path"]).read_bytes()).hexdigest() == schema["ownerdiag_candidate_manifest_sha256"]
assert schema["ownerdiag_normalization_sha256"] == "6560500e276e4782fb981726fc44c6ba6b8e18c2f6305193dd9833b9907511ad"
assert hashlib.sha256((ROOT / schema["ownerdiag_normalization_path"]).read_bytes()).hexdigest() == schema["ownerdiag_normalization_sha256"]
assert [x["path"] for x in schema["ownerdiag_source_pins"]] == [
    "shim/nord2-xnu/wifi_main_wfdma_native.h",
    "shim/nord2-xnu/wifi_main_wfdma_bind.inc",
    "shim/nord2-xnu/wifi_main_bind.inc"]
assert [x["sha256"] for x in schema["ownerdiag_source_pins"]] == [
    "3f24d54258a456b842745ff10f80ae86438e88328ee835d3a9494b0a7fb99433",
    "e214cdb762b8f638016b6c3eec59d5b5e694d61a7e01eaadef59c8460cb30818",
    "eeea6704878445f23e6eb94ecb26bc5f68e9dfbc99a08055c59567c201fe4766"]
for pin in schema["ownerdiag_source_pins"]:
    assert hashlib.sha256((ROOT / pin["path"]).read_bytes()).hexdigest() == pin["sha256"]
review_path=ROOT / "research/nord2/wifi/next-stage/main-fw-transport/init-event-format/manifest.json"
assert hashlib.sha256(review_path.read_bytes()).hexdigest()=="e174323a5dd5605c3a8ea7569e34ebd29e733fecb5e493de432daa7dbf89901d"
review=json.loads(review_path.read_text())
assert set(review["modified"])=={"wifi_main_transport.c","wifi_main_phy_force.c",
                                "wifi_main_layout.c","wifi_main_wfdma_bind.inc"}
assert review["new_markers"]==[]
for key,value in schema.items():
    if key.endswith("_path") and key != "ownerdiag_candidate_manifest_path":
        stem=key[:-5]
        assert value and not Path(value).is_absolute() and ".." not in Path(value).parts
        assert hashlib.sha256((ROOT/value).read_bytes()).hexdigest()==schema[stem+"_sha256"]
for name,stem in (("wifi_main_transport.c","rxformat_transport_source"),
                  ("wifi_main_phy_force.c","rxformat_phy_source"),
                  ("wifi_main_layout.c","rxformat_layout_source")):
    assert schema[stem+"_sha256"]==review["modified"][name]["candidate"]
v2=json.loads((ROOT/schema["ownerdiag_candidate_manifest_path"]).read_text())
assert v2["baseline"]["wifi_main_wfdma_bind.inc"]==review["modified"]["wifi_main_wfdma_bind.inc"]["candidate"]
assert schema["rxformat_native_finish_source_sha256"]==schema["ownerdiag_source_pins"][1]["sha256"]
assert not any(P.glob("*.img")) and not (P / "payload.bin").exists()
assert not any((P / name).exists() for name in
    ("manifest.json", "image-build.log", "flash.log", "client-hardware.log",
     "hardware-live", "preserved-failed-boot.log", "authorization-used.json"))
assert approval["scope"] == "boot-with-main-wifi-ownerdiag-calibration"
assert approval["authorized_at"] is None
build = (P / "build-image.sh").read_text()
assert "--enable-nord2wifimain" in build and '"$temp/main.adt"' in build
assert '"$temp/main.adt" "$temp/payload.bin"' in build
assert "nord2-ios-wifi-main-ownerdiag-battery-stop23.img" in build
verify = (P / "verify-image.py").read_text()
runner = (P / "run-approved-boot.py").read_text()
assert "nord2-ios-wifi-main-ownerdiag-battery-stop23.img" in verify
assert 'approval["scope"] == "boot-with-main-wifi-ownerdiag-calibration"' in runner
assert 'approval["image_sha256"] == prepared["image_sha256"] == manifest["image_sha256"]' in runner
assert 'approval["image_sha256"] != "8f1824f18a3ec8a70d75dbbcc65a8f2075da2fe12259253c1a0896786e9bcf0b"' in runner
assert '--authorized-wifi-main-ownerdiag-calibration-boot' in runner
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
print("PASS: owner diagnostic image stage closed, exact e0d3 fallback, distinct image approval")
