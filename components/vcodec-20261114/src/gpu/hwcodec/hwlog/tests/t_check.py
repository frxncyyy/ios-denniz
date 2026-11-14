#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""X01 Python-side checks: log2script and diff_logs against literal inputs."""

import os
import subprocess
import sys

PASS = 0
TOTAL = 0
FAIL = 0


def check(cond, name, detail=""):
    global PASS, TOTAL, FAIL
    TOTAL += 1
    if cond:
        PASS += 1
        print("X01 PY CK ok: %s" % name)
    else:
        FAIL += 1
        print("X01 PY CK FAIL: %s%s" % (name, (": " + detail) if detail else ""))


def run(cmd, expect=0):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr


def write(path, text):
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def main(argv):
    if len(argv) != 7:
        print(
            "usage: t_check.py HWDIR WORKDIR PHONE_LOG SCRIPT SUMMARY MUTANT_LOG",
            file=sys.stderr,
        )
        return 2
    hwdir, work, phone, script, summary, mutant = argv[1:]

    log2script = os.path.join(hwdir, "log2script.py")
    diff_logs = os.path.join(hwdir, "diff_logs.py")
    py = sys.executable

    # ---- generated script content ----
    text = open(script, encoding="utf-8").read()
    check(
        "read VENC 0x0024 returns 0xdeadbeef once" in text,
        "script has the persistent read rule as a one-shot",
    )
    check(
        text.count("read VENC 0x0028 returns 0x11111111 once") == 1,
        "script has exactly one 0x0028 once rule",
    )
    check(
        text.count("read VENC 0x0034 returns 0x00000002 once") == 2,
        "script has two 0x0034 one-shots (poll then read)",
    )
    check(
        "read VENC 0x0010" not in text,
        "script does not override 0x0010 (writes reproduce it)",
    )
    check("irq 0 status 0x00000001" in text, "script has IRQ 0 status")
    check("irq 2 status 0x00000010" in text, "script has IRQ 2 status")
    check(
        "irq 0 fires 4 ms after write VENC 0x0030 0x00000001" in text,
        "script has the 4 ms IRQ trigger from timestamps",
    )
    allowed = ("read ", "irq ", "set ", "#")
    check(
        all((not ln.strip()) or ln.startswith(allowed) for ln in text.splitlines()),
        "script contains only T02 grammar lines and comments",
    )
    check(
        "read VENC 0x0038" not in text,
        "timed-out poll does not invent a read value",
    )

    # ---- summary content ----
    stext = open(summary, encoding="utf-8").read()
    check(
        "VENC 0x0010 writes=2 reads=1 polls=0 first=0x00000001 last=0x00000003 "
        "masks=0xffffffff,0x0000000f read_values=0x00000003" in stext,
        "summary row 0x0010 is exact",
    )
    check(
        "VENC 0x0020 writes=0 reads=1 polls=1" in stext and
        "read_values=0x12345678" in stext,
        "summary counts the poll and its read value",
    )
    check(
        "VENC 0x0038 writes=0 reads=0 polls=1" in stext and
        "read_values=-" in stext,
        "summary does not invent a timeout read value",
    )
    rows = [ln for ln in stext.splitlines() if ln and not ln.startswith("#")]
    check(
        rows and rows[0].startswith("VENC 0x0010 "),
        "summary rows sorted by window then offset",
    )

    # ---- log2script on a raw T02 log (no timestamps) ----
    raw = write(
        os.path.join(work, "raw.log"),
        "POWER VENC on -> 0\n"
        "W VENC 0x0010 0x00000001\n"
        "R VENC 0x0010 0x00000001\n"
        "IRQ wait 0 timeout_ms=20 -> 0 status=0x00000002\n",
    )
    rc, out, err = run([py, log2script, raw, "--script", "-"])
    check(rc == 0, "raw T02 log converts (rc=0)", "rc=%d err=%s" % (rc, err))
    check(
        "irq 0 fires 0 ms after write VENC 0x0010 0x00000001" in out,
        "raw log IRQ trigger defaults to 0 ms",
    )
    check("irq 0 status 0x00000002" in out, "raw log IRQ status is kept")
    check("read VENC 0x0010" not in out, "raw log write/read pair needs no override")

    # ---- malformed input ----
    mixed = write(
        os.path.join(work, "mixed.log"),
        "1000 FOO bar\n1001 W VENC 0x0010 0x00000001 -> 0\n",
    )
    rc, out, err = run([py, log2script, mixed, "--script", "-"])
    check(rc == 0 and "unrecognised" in err, "mixed log warns and exits 0")
    badreg = write(
        os.path.join(work, "badreg.log"),
        "1000 R VENC 0x0001 0x12345678 -> 0\n"
        "1001 IRQ wait 0 timeout_ms=5 -> 0 status=0x1\n",
    )
    rc, out, err = run([py, log2script, badreg, "--script", "-"])
    check(
        rc == 0 and "read VENC 0x0001" not in out and "ignored" in err,
        "misaligned read is warned about and not emitted",
        "rc=%d out=%s err=%s" % (rc, out, err),
    )
    failed = write(
        os.path.join(work, "failed.log"),
        "1000 R VENC 0x0010 ? -> -13\n"
        "1001 W VENC 0x0010 0x00000001 -> 0\n",
    )
    rc, out, err = run([py, log2script, failed, "--script", "-"])
    check(
        rc == 0 and "read VENC 0x0010" not in out and "unrecognised" not in err,
        "failed read parses, emits no value and does not warn",
        "rc=%d out=%s err=%s" % (rc, out, err),
    )
    bad = write(os.path.join(work, "bad.log"), "1000 FOO bar\n")
    rc, out, err = run([py, log2script, bad, "--script", "-"])
    check(rc == 3, "unknown-only log exits 3", "rc=%d" % rc)
    rc, out, err = run([py, log2script, os.path.join(work, "missing.log"), "--script", "-"])
    check(rc == 2, "missing input exits 2", "rc=%d" % rc)
    rc, out, err = run([py, log2script, phone, "--script", "-", "--summary", "-"])
    check(rc == 2, "two stdout outputs rejected", "rc=%d" % rc)

    # ---- diff_logs: identical and one changed write ----
    rc, out, err = run([py, diff_logs, phone, phone])
    check(rc == 0 and "N2DIFF identical writes=4" in out, "identical log is reported identical")
    rc, out, err = run([py, diff_logs, phone, mutant])
    check(rc == 1, "mutant log exits 1", "rc=%d" % rc)
    check(
        "A line 12: 1011000 W VENC 0x0040 0x0000000a -> 0" in out,
        "mutant reports the exact A line",
        out,
    )
    check(
        "B line 12: 1011000 W VENC 0x0040 0x0000000b -> 0" in out,
        "mutant reports the exact B line",
        out,
    )

    # ---- DMA pool base normalisation ----
    a = write(
        os.path.join(work, "base-a.log"),
        "1000 DMA alloc handle=1 pa=0xda800000 bytes=4096 flags=0x0 -> 0\n"
        "1001 DMA alloc handle=2 pa=0xda801000 bytes=8192 flags=0x0 -> 0\n"
        "1010 W VENC 0x0100 0xda800000 -> 0\n"
        "1020 W VENC 0x0104 0xda801000 -> 0\n"
        "1030 R VENC 0x0108 0x00000001 -> 0\n",
    )
    b = write(
        os.path.join(work, "base-b.log"),
        "2000 DMA alloc handle=1 pa=0xdb000000 bytes=4096 flags=0x0 -> 0\n"
        "2001 DMA alloc handle=2 pa=0xdb001000 bytes=8192 flags=0x0 -> 0\n"
        "2010 W VENC 0x0100 0xdb000000 -> 0\n"
        "2020 W VENC 0x0104 0xdb001000 -> 0\n"
        "2030 R VENC 0x0108 0x00000002 -> 0\n",
    )
    rc, out, err = run([py, diff_logs, a, b])
    check(rc == 0 and "identical writes=2" in out, "pool-base-only differences are ignored")
    rc, out, err = run([py, diff_logs, "--all", a, b])
    check(rc == 1 and "R VENC 0x0108 0x00000001" in out,
          "--all reports the read difference")
    rc, out, err = run([py, diff_logs, "--base-a", "0xda800000", "--base-b", "0xdb000000", a, b])
    check(rc == 0, "explicit bases give the same result")

    # ---- masked writes compare by the masked field ----
    m1 = write(os.path.join(work, "m1.log"), "1 W VENC 0x0010 0x00000003 mask 0x0000000f -> 0\n")
    m2 = write(os.path.join(work, "m2.log"), "2 W VENC 0x0010 0x000000f3 mask 0x0000000f -> 0\n")
    m3 = write(os.path.join(work, "m3.log"), "3 W VENC 0x0010 0x00000005 mask 0x0000000f -> 0\n")
    rc, out, err = run([py, diff_logs, m1, m2])
    check(rc == 0, "bits outside the mask are ignored")
    rc, out, err = run([py, diff_logs, m1, m3])
    check(rc == 1, "bits inside the mask are compared")

    # ---- raw T02 recorder file vs X01 phone-format file ----
    raw2 = write(
        os.path.join(work, "raw2.log"),
        "POWER VENC on -> 0\n"
        "W VENC 0x0010 0x00000001\n"
        "R VENC 0x0010 0x00000001\n",
    )
    x01 = write(
        os.path.join(work, "x01.log"),
        "100 POWER VENC on -> 0\n"
        "101 W VENC 0x0010 0x00000001 -> 0\n"
        "102 R VENC 0x0010 0x00000001 -> 0\n",
    )
    rc, out, err = run([py, diff_logs, raw2, x01])
    check(rc == 0 and "identical writes=1" in out, "raw T02 and X01 logs align")

    # ---- length and timestamp handling ----
    extra = write(os.path.join(work, "extra.log"),
                  "9 W VENC 0x0010 0x00000001 -> 0\n"
                  "10 W VENC 0x0014 0x00000002 -> 0\n")
    one = write(os.path.join(work, "one.log"), "99 W VENC 0x0010 0x00000001 -> 0\n")
    rc, out, err = run([py, diff_logs, extra, one])
    check(rc == 1 and "B: <no such operation>" in out, "missing write is reported")
    ts1 = write(os.path.join(work, "ts1.log"), "100 W VENC 0x0010 1 -> 0\n")
    ts2 = write(os.path.join(work, "ts2.log"), "999999 W VENC 0x0010 1 -> 0\n")
    rc, out, err = run([py, diff_logs, ts1, ts2])
    check(rc == 0, "timestamps are ignored")

    print("X01 PY %s %d/%d" % ("PASS" if FAIL == 0 else "FAIL", PASS, TOTAL))
    return 0 if FAIL == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
