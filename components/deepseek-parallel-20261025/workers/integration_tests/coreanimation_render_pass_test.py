#!/usr/bin/env python3
"""Build and run the independent CoreAnimation-shaped render-pass test.

Release and ASan/UBSan configurations are both required.  The test is
expected to fail on the pre-fix HEAD because it uses natural non-64-aligned
display row strides and the deferred Unknown store action.  No GPU, phone,
native library rebuild, production edit or publication is involved.
"""
from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WORK = HERE.parents[4]
GPU = WORK / "gpu"
SOURCE = HERE / "coreanimation_render_pass_test.m"
BUILD = HERE / "build"
RUNS = HERE / "runs"
CLANG = os.environ.get("CLANG", shutil.which("clang") or "clang")


def run_case(sanitize: bool) -> tuple[subprocess.CompletedProcess[str] | None, str]:
    suffix = "_asan" if sanitize else ""
    exe = BUILD / f"coreanimation_render_pass_test{suffix}"
    cmd = [
        CLANG, "-x", "objective-c", "-std=gnu11", "-O1", "-g",
        "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
        "-Wno-unused-function", "-Wno-objc-root-class",
        "-I" + str(GPU / "metal"), "-I" + str(GPU / "include"),
    ]
    if sanitize:
        cmd += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    cmd += [str(SOURCE), "-lobjc", "-o", str(exe)]
    build = subprocess.run(cmd, capture_output=True, text=True)
    if build.returncode:
        return None, (
            f"{'ASan' if sanitize else 'release'} build rc={build.returncode}\n"
            + build.stdout + build.stderr
        )
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0",
               UBSAN_OPTIONS="halt_on_error=1")
    proc = subprocess.run([str(exe)], capture_output=True, text=True, env=env)
    return proc, proc.stdout + proc.stderr


def main() -> int:
    BUILD.mkdir(parents=True, exist_ok=True)
    RUNS.mkdir(parents=True, exist_ok=True)
    if not SOURCE.is_file():
        print(f"FAIL missing {SOURCE}", file=sys.stderr)
        return 2
    failed = False
    for sanitize in (False, True):
        tag = "asan" if sanitize else "release"
        proc, text = run_case(sanitize)
        log = RUNS / f"delta11_coreanimation_{tag}.log"
        log.write_text(text)
        print(f"== CoreAnimation pass {tag} ==")
        print(text.strip())
        match = re.search(
            r"COREANIMATION_PASS checks=(\d+) failures=(\d+)", text)
        if not match:
            print(f"FAIL {tag}: summary missing", file=sys.stderr)
            failed = True
            continue
        checks, failures = int(match.group(1)), int(match.group(2))
        if proc is None or proc.returncode != 0 or failures:
            print(f"FAIL {tag}: checks={checks} failures={failures} "
                  f"rc={None if proc is None else proc.returncode}",
                  file=sys.stderr)
            failed = True
        else:
            print(f"PASS {tag}: checks={checks} failures=0")
        if proc is not None and proc.returncode not in (0, 1):
            failed = True
    if failed:
        print("COREANIMATION_PASS_RESULT FAIL "
              "expected red on pre-fix HEAD; green requires the production "
              "CoreAnimation stride/pass admission and deferred-store fix",
              file=sys.stderr)
        return 1
    print("COREANIMATION_PASS_RESULT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
