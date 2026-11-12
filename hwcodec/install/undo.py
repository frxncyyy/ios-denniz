#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Undo gpu/hwcodec's install: remove exactly the files in /private/var/db/nord2-vcodec/install.json.

    undo.py ROOT [--purge-vendor]

ROOT is "/" on the phone or the mount point of the root image.  The vendor directory is kept by default (the five files are
the user's own phone files); --purge-vendor removes the directory too.  Nothing outside the receipt is touched.
The launchd job is not stopped here: on a live phone the main session runs
    launchctl bootout system/com.nord2.vcodecd
first (or after, if the files must disappear under a stopped job).
"""
import argparse
import json
import os
import sys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--purge-vendor", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    receipt_path = os.path.join(root, "private/var/db/nord2-vcodec/install.json")
    if not os.path.exists(receipt_path):
        print("VCODECUNDO nothing to do (%s absent)" % receipt_path)
        return 0
    try:
        receipt = json.load(open(receipt_path))
    except (OSError, ValueError) as exc:
        sys.exit("undo.py: cannot read %s: %s" % (receipt_path, exc))
    for rel in sorted(receipt.get("files", {})):
        full = os.path.join(root, rel.lstrip("/"))
        if os.path.isfile(full):
            os.unlink(full)
            print("VCODECUNDO removed", rel)
        else:
            print("VCODECUNDO absent", rel)
    os.unlink(receipt_path)
    print("VCODECUNDO removed private/var/db/nord2-vcodec/install.json")
    if args.purge_vendor:
        vendor = os.path.join(root, "private/var/db/nord2-vcodec/vendor")
        if os.path.isdir(vendor):
            for base, dirs, files in os.walk(vendor, topdown=False):
                for name in files:
                    p = os.path.join(base, name)
                    os.unlink(p)
                    print("VCODECUNDO removed " + os.path.relpath(p, root))
                for name in dirs:
                    p = os.path.join(base, name)
                    if os.path.islink(p):
                        os.unlink(p)
                    else:
                        os.rmdir(p)
            os.rmdir(vendor)
            print("VCODECUNDO removed private/var/db/nord2-vcodec/vendor")
        db = os.path.join(root, "private/var/db/nord2-vcodec")
        if os.path.isdir(db) and not os.listdir(db):
            os.rmdir(db)
            print("VCODECUNDO removed private/var/db/nord2-vcodec")
    print("VCODEC UNDO OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
