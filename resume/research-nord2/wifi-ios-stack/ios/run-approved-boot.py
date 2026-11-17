#!/usr/bin/env python3
"""One-use future runner. Closed gates prevent any device access by default."""
from pathlib import Path
import datetime
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

assert sys.argv[1:] == ["--authorized-wifi-ios-stack-boot"]
OUT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("main_image_verify", OUT / "verify-image.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
identity = verify.load_identity()
verify.load_schema()
prepared = json.loads((OUT / "prepared-image-required.json").read_text())
approval = json.loads((OUT / "authorization-required.json").read_text())
manifest = json.loads((OUT / "manifest.json").read_text())
assert approval["image_sha256"] != "c3ce7dcfcce13085579e59998b6f29a9fb0e2b11a7a3420b9cc97db5a8ab3f1a"
assert approval["image_sha256"] != "89b3eb2abe4e906f420263803033a2a33a3717b23aecdafd488344e168e42066"
assert approval["image_sha256"] != "f53ad2b7875ed7435b1cbaaf3b9173df72565459295b7ea91fb6b9c9d8653bda"
assert approval["image_sha256"] != "1c5aa596436e98e74cbc3b2bf72ed66d004ba49599ad77f63f452ceae029eb56"
assert approval["image_sha256"] != "c31ff5e47611fac18d6eb242cab86f39fa049d751ba0a9e0fa65ed3658ad8b1f"
assert approval["image_sha256"] != "58f62ae6c3e7650bd71d9c8f723360602ee6ac90ff1a21d03e269ec8ca769b4c"
assert approval["image_sha256"] != "a199cd0cda7138ba2ecf39e3bc585cbbd3556e12f3b7169d81894bdccf985562"
assert approval["image_sha256"] != "195a7e174c750f1e0b2bd54894c615ce9e008068b8a52e609923246f6a3ed09b"
assert prepared["parent_verified"] is True
assert approval["human_exact_image_authorized"] is True
assert approval["rf_calibration_authorized"] is True
assert approval["conditional_recovery_authorized"] is True
assert approval["scope"] == "boot-with-wifi-ios-stack"
assert approval["image_sha256"] == prepared["image_sha256"] == manifest["image_sha256"]
assert approval["image_sha256"] != "52d4b1591b3b8d906214ec37bc93e309d5a3b69d59fd8a013c424d755a504aca"
assert approval["image_sha256"] != "6ddf8c15dd33ccdf84131b356a54f9857925c8e3f52391f4701368aa50eee3e9"
assert approval["image_sha256"] != "fa13be9baa232b26c89994998cd566fa2ec4e0d96748b5420f1c6504ba203be7"
assert manifest["ci_run"] == identity["ci_run"]
assert manifest["expected_kernel_source"] == identity["source_commit"]
assert manifest["kernel_sha256"] == identity["kernel_sha256"]
assert manifest["commandline"] == verify.COMMANDLINE
assert manifest["mode"] == verify.MODE
assert manifest["wifi_main_firmware_requested"] is True
assert manifest["wifi_phy_force_cal_requested"] is True
assert manifest["wifi_capability_query_requested"] is True
assert manifest["wifi_basic_config_requested"] is True
assert manifest["wifi_basic_config_limit"] == 1
assert manifest["wifi_basic_config_response_expected"] is False
assert manifest["wifi_dma_enabled"] is True
assert manifest["wifi_scan_requested"] is True
assert manifest["wifi_normal_init_commands"] == 26
assert manifest["wifi_vcn33_configuration_requested"] is True
assert manifest["wifi_vcn33_mon3_required"] is False
assert manifest["wifi_rf_nonzero_required"] is False
assert verify.COMMANDLINE.endswith(" nord2wifiios=1")
assert manifest["wifi_passive_scan_only"] is True
assert manifest["wifi_network_interface_requested"] is True
assert isinstance(approval["device_serial"], str) and approval["device_serial"]
assert prepared["image_path"] == str(verify.IMAGE.relative_to(verify.ROOT))

def checked_file(path, size, digest):
    assert isinstance(digest, str) and len(digest) == 64 and size > 0
    assert path.stat().st_size == size
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest

checked_file(verify.IMAGE, prepared["image_bytes"], prepared["image_sha256"])
fallback = json.loads((OUT / "baseline.json").read_text())
working = json.loads((OUT / "recovery-image.json").read_text())
checked_file(verify.ROOT / working["path"], working["bytes"], working["sha256"])
checked_file(verify.ROOT / "research/nord2/rescue/nord2-adb-rescue.img",
             28884992, fallback["ram_rescue_sha256"])
checked_file(verify.ROOT / "backup/nord2/boot.img",
             33554432, "04e84c4a14e5c3cb77d0e587da93b5a7e720e8c5b2a3a51ab3ef4cda746e1f01")
subprocess.run([sys.executable, str(OUT / "verify-image.py"), "--check-existing"],
               cwd=verify.ROOT, check=True)
installed = json.loads((verify.ROOT / "research/nord2/ios-userland-rw/install-bounded/result.json").read_text())
root_hash = "8b1d9de6775674afe790a161419ea23e824c5e045f21a66c6fc886125c991cb3"
assert installed["userdata_write_completed"] and installed["full_readback_verified"]
assert installed["image_sha256"] == root_hash and installed["image_bytes"] == 17179869184
assert (verify.ROOT / "research/nord2/ios-userland-rw/install-bounded/device-readback.sha256").read_text().split()[0] == root_hash
assert not (OUT / "hardware-live").exists()
assert not any((OUT / name).exists() for name in ("flash.log", "client-hardware.log", "client-hardware.pid"))
assert approval["image_sha256"] != "0e949521b82795c69d8539043cc3c921c31f9de265cee6b72d12e90aff9877a2"
with (OUT / "RUNNER_EXECUTION_USED").open("x") as marker:
    marker.write(datetime.datetime.now().astimezone().isoformat()+"\n")
serial = approval["device_serial"]
fastboot = "$HOME/Android/Sdk/platform-tools/fastboot"
devices = subprocess.run([fastboot, "devices"], capture_output=True, text=True,
                         timeout=10, check=True)
assert [line.split() for line in devices.stdout.splitlines() if line.strip()] == [[serial, "fastboot"]]
with (OUT / "flash.log").open("x") as log:
    def run(*args):
        result = subprocess.run([fastboot, "-s", serial, *args],
                                capture_output=True, text=True, timeout=120)
        captured = result.stdout + result.stderr
        log.write(datetime.datetime.now().astimezone().isoformat() + " " + repr(args) + "\n" + captured)
        log.flush()
        print(captured, flush=True)
        result.check_returncode()
        return captured
    assert "product: oplus6893" in run("getvar", "product")
    assert "partition-size:boot: 2000000" in run("getvar", "partition-size:boot")
    assert "is-userspace: no" in run("getvar", "is-userspace")
    run("flash", "boot", str(verify.IMAGE))
    with (OUT / "client-hardware.log").open("x") as client_log:
        client = subprocess.Popen([sys.executable, "tools/nord2-usb-console.py",
                                   str(OUT / "hardware-live"), "--wait", "300",
                                   "--read-seconds", "300", "--timeout-ms", "5000",
                                   "--timeout-retries", "5"], cwd=verify.ROOT,
                                  stdout=client_log, stderr=subprocess.STDOUT)
        (OUT / "client-hardware.pid").write_text(str(client.pid) + "\n")
        run("reboot")
        code = client.wait()
        print(json.dumps({"usb_client_exit": code}), flush=True)
        # No automatic rescue or Linux handoff. Recovery is separately gated.
        raise SystemExit(code)
