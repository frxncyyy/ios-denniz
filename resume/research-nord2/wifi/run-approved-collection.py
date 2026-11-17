#!/usr/bin/env python3
"""Private two-stage boot-only firmware collection; requires human approval."""
from pathlib import Path
import argparse
import datetime
import hashlib
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--authorized-firmware-collection", action="store_true", required=True)
parser.add_argument("stage", choices=("rescue", "return-ios"))
args = parser.parse_args()
out = Path("research/nord2/wifi")
fb = "$HOME/Android/Sdk/platform-tools/fastboot"
serial = "<redacted-serial>"  # Private; never commit.
images = {
    "rescue": (Path("research/nord2/rescue/nord2-adb-rescue.img"), 28884992,
               "d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5"),
    "return-ios": (Path("shim/out/nord2-xnu/nord2-ios-input-power-stop23.img"), 33544192,
                   "fd6d75caf4c7e95431bdb40eb50ca79650e8272824f54fd5f783cdec49995f20"),
}
for path, size, digest in images.values():
    with path.open("rb") as data:
        actual = hashlib.file_digest(data, "sha256").hexdigest()
    if path.stat().st_size != size or actual != digest:
        raise SystemExit("Frozen boot image verification failed")
if args.stage == "return-ios" and not (out / "rescue-rebooted.txt").exists():
    raise SystemExit("No successful collection rescue boot recorded")
devices = subprocess.check_output([fb, "devices"], text=True, timeout=10)
if [line.split() for line in devices.splitlines() if line.strip()] != [[serial, "fastboot"]]:
    raise SystemExit("Expected Nord2 alone in fastboot")
with (out / (args.stage + "-flash.log")).open("x") as log:
    def run(*command):
        result = subprocess.run([fb, "-s", serial, *command], text=True,
                                capture_output=True, timeout=120)
        text = result.stdout + result.stderr
        log.write(datetime.datetime.now().astimezone().isoformat() + " " +
                  repr(command) + "\n" + text)
        log.flush()
        print(text, flush=True)
        result.check_returncode()
        return text
    for variable, expected in [
        ("product", "product: oplus6893"),
        ("partition-size:boot", "partition-size:boot: 2000000"),
        ("is-userspace", "is-userspace: no"),
    ]:
        if expected not in run("getvar", variable):
            raise SystemExit("Fastboot device/partition check failed")
    run("flash", "boot", str(images[args.stage][0]))
    run("reboot")
    (out / (args.stage + "-rebooted.txt")).write_text(
        datetime.datetime.now().astimezone().isoformat() + "\n")
