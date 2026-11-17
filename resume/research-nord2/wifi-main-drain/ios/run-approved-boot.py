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

assert sys.argv[1:] == ["--authorized-wifi-main-drain-calibration-boot"]
OUT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("main_image_verify", OUT / "verify-image.py")
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
identity = verify.load_identity()
verify.load_schema()
prepared = json.loads((OUT / "prepared-image-required.json").read_text())
approval = json.loads((OUT / "authorization-required.json").read_text())
manifest = json.loads((OUT / "manifest.json").read_text())
assert prepared["parent_verified"] is True
assert approval["human_exact_image_authorized"] is True
assert approval["rf_calibration_authorized"] is True
assert approval["scope"] == "boot-with-main-wifi-drain-calibration"
assert approval["image_sha256"] == prepared["image_sha256"] == manifest["image_sha256"]
assert approval["image_sha256"] != "0ce012687f2e8266a9ca28edc18101e772ef34db304874b85526a00d279f9ea5"
assert manifest["ci_run"] == identity["ci_run"]
assert manifest["expected_kernel_source"] == identity["source_commit"]
assert manifest["kernel_sha256"] == identity["kernel_sha256"]
assert manifest["commandline"] == verify.COMMANDLINE
assert manifest["mode"] == verify.MODE
assert manifest["wifi_main_firmware_requested"] is True
assert manifest["wifi_phy_force_cal_requested"] is True
assert manifest["wifi_dma_enabled"] is True
assert manifest["wifi_network_interface_requested"] is False
assert isinstance(approval["device_serial"], str) and approval["device_serial"]
assert prepared["image_path"] == str(verify.IMAGE.relative_to(verify.ROOT))

def checked_file(path, size, digest):
    assert isinstance(digest, str) and len(digest) == 64 and size > 0
    assert path.stat().st_size == size
    assert hashlib.sha256(path.read_bytes()).hexdigest() == digest

checked_file(verify.IMAGE, prepared["image_bytes"], prepared["image_sha256"])
fallback = json.loads((OUT / "baseline.json").read_text())
checked_file(verify.ROOT / "shim/out/nord2-xnu/nord2-ios-wifi-dma-battery-stop23.img",
             fallback["fallback_image_bytes"], fallback["fallback_image_sha256"])
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
                                   "--read-seconds", "180", "--timeout-ms", "5000",
                                   "--timeout-retries", "5"], cwd=verify.ROOT,
                                  stdout=client_log, stderr=subprocess.STDOUT)
        (OUT / "client-hardware.pid").write_text(str(client.pid) + "\n")
        run("reboot")
        code = client.wait()
        print(json.dumps({"usb_client_exit": code}), flush=True)
        # No automatic rescue or Linux handoff. Recovery is separately gated.
        raise SystemExit(code)
