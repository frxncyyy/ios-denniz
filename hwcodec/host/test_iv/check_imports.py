#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""check_imports.py -- cross-check n2vhost's unresolved list against D01's import oracle.

D01 owns facts/imports.py, which lists the 149 symbols the five vendor objects import from outside
their own set (read from the extracted /vendor at run time).  n2vhost prints, for each registered
host-call slot whose name no shim table provides, one "N2VHOST unresolved NAME" line, and with
--dump-shims every table entry as "N2VHOST shim NAME".

This script asserts the bookkeeping is exact:
    unresolved == (imports - {2 data imports, __aeabi_unwind_cpp_pr0} - shim names)
and that no name outside the oracle's list is ever reported unresolved.  Both sides are independent:
the right-hand side comes from D01's ELF analysis, the left-hand side from the loader through the
frozen hostcall API.

usage: check_imports.py <runner-output-file> [--imports PATH] [--vendor DIR]
Last line: "IV-CHECK imports=N unresolved=M expected=M ok" (or "FAIL <why>"), exit 0/1.
"""
import argparse
import os
import subprocess
import sys


def parse_oracle(path, vendor):
    """Run D01's oracle and return the set of external import names."""
    env = dict(os.environ)
    if vendor:
        env["N2HW_VENDOR_DIR"] = vendor
    out = subprocess.run([sys.executable, path], env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE, text=True)
    if out.returncode != 0:
        raise SystemExit("IV-CHECK FAIL oracle exited %d: %s" % (out.returncode, out.stderr.strip()))
    names = set()
    for line in out.stdout.splitlines():
        if line.startswith("  ") and len(line) > 2 and not line.startswith("    "):
            name = line.split()[0]
            if name and not name.startswith("["):
                names.add(name)
    return names, out.stdout


def parse_runner(path):
    unresolved, shims, milestone = set(), set(), None
    with open(path, "r", errors="replace") as f:
        for line in f:
            if line.startswith("N2VHOST unresolved "):
                unresolved.add(line.split(None, 2)[2].strip())
            elif line.startswith("N2VHOST shim "):
                shims.add(line.split(None, 2)[2].strip())
            elif line.startswith("N2VHOST reached M-"):
                milestone = line.strip()
    return unresolved, shims, milestone


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("output")
    ap.add_argument("--imports", default=None)
    ap.add_argument("--vendor", default=None)
    args = ap.parse_args()

    here = os.path.dirname(os.path.abspath(__file__))
    hwd = os.path.dirname(os.path.dirname(here))
    oracle = args.imports or os.path.join(hwd, "facts", "imports.py")
    if not os.path.exists(oracle):
        print("IV-CHECK FAIL oracle not found: %s" % oracle)
        return 1

    imports, raw = parse_oracle(oracle, args.vendor)
    if not imports:
        print("IV-CHECK FAIL oracle printed no import names")
        return 1
    unresolved, shims, milestone = parse_runner(args.output)
    if milestone is None:
        print("IV-CHECK FAIL no milestone line in %s" % args.output)
        return 1

    # The two OBJECT imports come from a data table; __aeabi_unwind_cpp_pr0 has no dynamic relocation
    # (FACTS 3.3, 3.4), so none of them can appear as a hostcall slot.
    never_slots = {"__stack_chk_guard", "stderr", "__aeabi_unwind_cpp_pr0"}
    expected = imports - never_slots - shims
    stray = unresolved - imports
    missing = expected - unresolved
    extra = unresolved - expected
    problems = []
    if stray:
        problems.append("%d name(s) not in the oracle: %s" % (len(stray), ", ".join(sorted(stray)[:5])))
    if missing:
        problems.append("%d expected unresolved name(s) absent: %s"
                        % (len(missing), ", ".join(sorted(missing)[:5])))
    if extra:
        problems.append("%d unexpected unresolved name(s): %s" % (len(extra), ", ".join(sorted(extra)[:5])))
    if problems:
        print("IV-CHECK imports=%d unresolved=%d expected=%d FAIL %s"
              % (len(imports), len(unresolved), len(expected), "; ".join(problems)))
        return 1
    print("IV-CHECK imports=%d unresolved=%d expected=%d ok" % (len(imports), len(unresolved), len(expected)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
