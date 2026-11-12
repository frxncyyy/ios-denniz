#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Install the vcodecd payload into a root file system.

Run by the main session, never by a worker.  ROOT is "/" on a live phone or the mount point of the iOS root image; the
payload (vcodecd, the launchd plist, the undo script) is read from the directory that holds this script and plan.json.

    install.py ROOT [--payload DIR] [--vendor DIR] [--no-receipt]

What it does, in order:
  1. verifies every payload file against plan.json's sha256;
  2. creates /usr/local/bin, /System/Library/LaunchDaemons, /var/log and /private/var/db/nord2-vcodec/vendor;
  3. refuses to replace an existing file unless it is in the previous receipt (or absent), so nobody else's file is lost;
  4. copies the binaries (0755 root:wheel) and the plist (0644 root:wheel), writes the receipt
     /private/var/db/nord2-vcodec/install.json;
  5. checks that the five vendor objects are present in the vendor directory and prints their sizes/sha256.  It does NOT
     copy them: they are the user's own phone files and the main session brings them from /vendor itself.

The launchd job is installed but not started (`RunAtLoad` false); start it with
    launchctl kickstart -k system/com.nord2.vcodecd
and watch /var/log/nord2-vcodecd.log for "N2VCD ... start".
"""
import argparse
import hashlib
import json
import os
import shutil
import sys

VENDOR_FILES = [
    ("bin/vpud", "vpud", 0o755),
    ("lib/libvpud_vcodec.so", "libvpud_vcodec.so", 0o755),
    ("lib/libvcodecdrv.so", "libvcodecdrv.so", 0o644),
    ("lib/libvcodec_utility.so", "libvcodec_utility.so", 0o644),
    ("lib/libvcodec_oal.so", "libvcodec_oal.so", 0o644),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def ensure_dir(root, path, mode):
    full = os.path.join(root, path.lstrip("/"))
    if not os.path.isdir(full):
        os.makedirs(full, exist_ok=True)
        print("VCODECDDIR", path, "created mode %o" % mode)
    os.chmod(full, mode)
    try:
        os.chown(full, 0, 0)
    except OSError as exc:
        if os.geteuid() == 0:
            sys.exit("install.py: chown %s: %s" % (full, exc))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--payload", default=os.path.dirname(os.path.abspath(__file__)))
    ap.add_argument("--vendor", default=None)
    ap.add_argument("--no-receipt", action="store_true")
    ap.add_argument("--force", action="store_true",
                    help="replace a file whose content changed if the previous receipt owns that path")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    payload = os.path.abspath(args.payload)
    plan_path = os.path.join(payload, "plan.json")
    if not os.path.isdir(root):
        sys.exit("install.py: root %s is not a directory" % root)
    try:
        plan = json.load(open(plan_path))
    except (OSError, ValueError) as exc:
        sys.exit("install.py: cannot read %s: %s" % (plan_path, exc))
    if plan.get("format") != 1:
        sys.exit("install.py: plan format %r, want 1" % plan.get("format"))

    db = "/private/var/db/nord2-vcodec"
    vendor = args.vendor or (db + "/vendor")
    receipt_path = db + "/install.json"
    receipt_full = os.path.join(root, receipt_path.lstrip("/"))
    old = {}
    if os.path.exists(receipt_full):
        try:
            old = json.load(open(receipt_full)).get("files", {})
        except (OSError, ValueError):
            sys.exit("install.py: existing receipt %s is unreadable; refusing to guess" % receipt_path)

    for rel, meta in plan["files"].items():
        src = os.path.join(payload, meta["payload"])
        if not os.path.isfile(src):
            sys.exit("install.py: payload file %s is missing" % src)
        got = sha256(src)
        if got != meta["sha256"]:
            sys.exit("install.py: %s sha256 %s, plan says %s" % (src, got, meta["sha256"]))

    ensure_dir(root, "/usr/local/bin", 0o755)
    ensure_dir(root, "/usr/local/libexec", 0o755)
    ensure_dir(root, "/System/Library/LaunchDaemons", 0o755)
    ensure_dir(root, "/var/log", 0o755)
    ensure_dir(root, db, 0o700)
    ensure_dir(root, vendor, 0o700)
    ensure_dir(root, vendor + "/bin", 0o755)
    ensure_dir(root, vendor + "/lib", 0o755)

    installed = {}
    for rel, meta in sorted(plan["files"].items()):
        src = os.path.join(payload, meta["payload"])
        dst = os.path.join(root, rel.lstrip("/"))
        if os.path.exists(dst):
            if os.path.islink(dst) or not os.path.isfile(dst):
                sys.exit("install.py: %s exists and is not a regular file" % rel)
            if rel not in old:
                sys.exit("install.py: %s exists and is not ours (no receipt entry); not replacing" % rel)
            if old.get(rel) != sha256(dst) and not args.force:
                sys.exit("install.py: %s exists and differs from the receipt; use --force to upgrade it" % rel)
        tmp = dst + ".n2codec-tmp"
        shutil.copyfile(src, tmp)
        os.chmod(tmp, meta["mode"])
        try:
            os.chown(tmp, 0, 0)
        except OSError as exc:
            if os.geteuid() == 0:
                sys.exit("install.py: chown %s: %s" % (tmp, exc))
        os.replace(tmp, dst)
        if sha256(dst) != meta["sha256"]:
            sys.exit("install.py: %s changed while installing" % rel)
        installed[rel] = meta["sha256"]
        print("VCODECDFILE %s mode %04o uid:gid 0:0 sha256 %s" % (rel, meta["mode"], meta["sha256"][:16]))

    if not args.no_receipt:
        # A partial payload must not orphan files the previous receipt owns: carry their entries forward while
        # the file is still the one the receipt recorded, so undo still removes them and a later install knows
        # they are ours.
        for rel, want in old.items():
            if rel in installed:
                continue
            dst = os.path.join(root, rel.lstrip("/"))
            if os.path.isfile(dst) and not os.path.islink(dst) and sha256(dst) == want:
                installed[rel] = want
        receipt = {"format": 1, "tag": plan.get("tag", ""), "files": installed,
                   "vendor_dir": vendor, "note": "gpu/hwcodec install; undo with /usr/local/libexec/nord2-vcodec-undo.py /"}
        tmp = receipt_full + ".tmp"
        with open(tmp, "w") as f:
            json.dump(receipt, f, indent=1)
            f.write("\n")
        os.replace(tmp, receipt_full)
        print("VCODECDRECEIPT", receipt_path, len(installed), "files")

    vdir = os.path.join(root, vendor.lstrip("/"))
    missing = []
    for rel, _name, mode in VENDOR_FILES:
        full = os.path.join(vdir, rel)                 # bin/vpud, lib/lib*.so: the vendor layout
        if not os.path.isfile(full):
            missing.append(rel)
            continue
        os.chmod(full, mode)
        try:
            os.chown(full, 0, 0)
        except OSError as exc:
            if os.geteuid() == 0:
                sys.exit("install.py: chown %s: %s" % (full, exc))
        print("VCODECVENDOR %s mode %04o sha256 %s" % (os.path.basename(rel), mode, sha256(full)[:16]))
    if missing:
        print("VCODECDMISSING vendor objects not in %s: %s" % (vendor, " ".join(missing)))
        print("VCODECDMISSING the daemon will refuse to start until the main session copies them from the phone's /vendor")
        return 3
    print("VCODEC INSTALL OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
