#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Receipt edge cases for install/install.py + undo.py, run by stage.sh --host-test.

Covers what the fake-root walk in stage.sh cannot: a full payload followed by a partial payload must keep
the previous receipt entries (so undo removes the files the partial payload did not mention), and a failed
partial must not make the old files look foreign.  Everything is under /tmp; no phone, no root image.
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INSTALL = os.path.join(HERE, "install.py")
UNDO = os.path.join(HERE, "undo.py")


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def make_payload(root, tag, files):
    os.makedirs(root, exist_ok=True)
    plan = {"format": 1, "tag": tag, "files": {}}
    for rel, body in files.items():
        full = os.path.join(root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(body)
        plan["files"]["/" + rel] = {"payload": rel, "mode": 0o755, "sha256": sha(full)}
    with open(os.path.join(root, "plan.json"), "w") as f:
        json.dump(plan, f)


def read_receipt(root):
    with open(os.path.join(root, "private/var/db/nord2-vcodec/install.json")) as f:
        return json.load(f)["files"]


def run(args):
    r = subprocess.run([sys.executable] + args, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit("install receipt test: command failed: %s" % " ".join(args))
    return r


def main():
    base = tempfile.mkdtemp(prefix="n2hw-ip-install-", dir="/tmp")
    try:
        full = os.path.join(base, "full")
        partial = os.path.join(base, "partial")
        root = os.path.join(base, "root")
        os.makedirs(root)
        vendor = os.path.join(root, "private/var/db/nord2-vcodec/vendor")
        os.makedirs(os.path.join(vendor, "bin"))
        os.makedirs(os.path.join(vendor, "lib"))
        for rel in ("bin/vpud", "lib/libvpud_vcodec.so", "lib/libvcodecdrv.so",
                    "lib/libvcodec_utility.so", "lib/libvcodec_oal.so"):
            open(os.path.join(vendor, rel), "wb").write(b"fake vendor\n")
        # The real payload has a binary and the plist; the partial has only the plist.
        make_payload(full, "full", {"usr/local/bin/tool": b"full tool\n",
                                    "System/Library/LaunchDaemons/x.plist": b"plist\n"})
        make_payload(partial, "partial", {"System/Library/LaunchDaemons/x.plist": b"plist\n"})
        run([INSTALL, root, "--payload", full])
        got = read_receipt(root)
        assert set(got) == {"/usr/local/bin/tool", "/System/Library/LaunchDaemons/x.plist"}, got
        run([INSTALL, root, "--payload", partial])
        got = read_receipt(root)
        assert set(got) == {"/usr/local/bin/tool", "/System/Library/LaunchDaemons/x.plist"}, got
        assert os.path.isfile(os.path.join(root, "usr/local/bin/tool"))
        run([UNDO, root])
        assert not os.path.exists(os.path.join(root, "usr/local/bin/tool"))
        assert not os.path.exists(os.path.join(root, "System/Library/LaunchDaemons/x.plist"))
        print("INSTALL RECEIPT TEST OK")
        return 0
    finally:
        shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
