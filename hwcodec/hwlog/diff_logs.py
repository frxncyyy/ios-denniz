#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Align two X01 n2vc_hw op logs and print the first divergent write.

PC fake run vs phone run is the intended use.  Timestamps are ignored.  A
write value that falls inside a DMA buffer allocation of its own log is
reduced by that log's DMA pool base, so two runs whose pools start at
different physical addresses still compare equal.  The pool base is inferred
as the lowest `DMA alloc pa=` value in the log; --base-a/--base-b override it.

Default mode compares the sequence of register writes (W, W mask) only.
--all compares every operation (power, reads, polls, IRQ, DMA, DELAY).

Exit status: 0 identical, 1 first divergence found, 2 usage/read error.
"""

import argparse
import sys
from typing import List, Optional, Tuple

# diff_logs imports log2script; never leave a __pycache__ in the source tree.
sys.dont_write_bytecode = True
from log2script import Entry, parse_file


def dma_region(entries: List[Entry]) -> Tuple[int, int]:
    """[lowest allocation, highest allocation end) of this log's DMA pool."""
    lo = None
    hi = None
    for e in entries:
        if e.op == "dma_alloc" and e.pa is not None:
            try:
                n = int(e.fields.get("bytes", "0"), 0)
            except ValueError:
                n = 0
            n = (n + 4095) & ~4095
            if lo is None or e.pa < lo:
                lo = e.pa
            if hi is None or e.pa + n > hi:
                hi = e.pa + n
    if lo is None or hi is None or hi <= lo:
        return (0, 0)
    return (lo, hi)


def infer_base(region: Tuple[int, int]) -> int:
    return region[0]


def round4k(val):
    try:
        return (int(val, 0) + 4095) & ~4095
    except (TypeError, ValueError):
        return val


def norm_addr(val: Optional[int], region: Tuple[int, int], base: int) -> Optional[int]:
    if val is None:
        return None
    lo, hi = region
    if lo <= val < hi:
        return val - base
    return val


def write_key(e: Entry, ranges, base):
    if e.win is None or e.off is None:
        return ("w", e.win, e.off, None, None, e.rc or 0)
    mask = e.mask if e.mask is not None else 0xFFFFFFFF
    eff = e.value & mask if e.value is not None else None
    return ("w", e.win, e.off, mask, norm_addr(eff, ranges, base), e.rc or 0)


def all_key(e: Entry, ranges, base):
    rc = e.rc or 0
    if e.op == "w":
        return write_key(e, ranges, base)
    if e.op == "power":
        return ("power", e.block, e.on, rc)
    if e.op == "r":
        return ("r", e.win, e.off, norm_addr(e.value, ranges, base), rc)
    if e.op == "poll":
        return ("poll", e.win, e.off, norm_addr(e.value, ranges, base), e.mask, e.ok, rc)
    if e.op == "delay":
        return ("delay", e.value, rc)
    if e.op == "irq_wait":
        return ("irq_wait", e.irq, e.timeout_ms, rc, e.status)
    if e.op == "irq_clear":
        return ("irq_clear", e.irq, rc)
    if e.op == "dma_alloc":
        return (
            "dma_alloc",
            e.handle,
            norm_addr(e.pa, ranges, base),
            round4k(e.fields.get("bytes")),
            e.flags,
            rc,
        )
    if e.op == "dma_free":
        return ("dma_free", e.handle, rc)
    if e.op == "dma_rw":
        return (
            "dma_rw",
            e.handle,
            e.off,
            e.fields.get("bytes"),
            e.direction,
            e.crc,
            rc,
        )
    return None


def select(entries, ranges, base, all_ops: bool):
    out = []
    for e in entries:
        if e.op in ("script", "hwlog", "unknown"):
            continue
        if not all_ops and e.op != "w":
            continue
        k = all_key(e, ranges, base)
        if k is not None:
            out.append((e, k))
    return out


def compare(a, b):
    n = min(len(a), len(b))
    for i in range(n):
        if a[i][1] != b[i][1]:
            return i, a[i][0], b[i][0]
    if len(a) != len(b):
        if len(a) > len(b):
            return n, a[n][0], None
        return n, None, b[n][0]
    return None


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="print the first divergent write between two n2vc_hw op logs"
    )
    ap.add_argument("a", help="log A (PC fake run)")
    ap.add_argument("b", help="log B (phone run)")
    ap.add_argument(
        "--all",
        action="store_true",
        help="compare every operation, not only register writes",
    )
    ap.add_argument("--base-a", type=lambda x: int(x, 0), help="DMA pool base of A")
    ap.add_argument("--base-b", type=lambda x: int(x, 0), help="DMA pool base of B")
    ap.add_argument("--verbose", action="store_true", help="print inferred bases")
    args = ap.parse_args(argv)

    try:
        ea, ua = parse_file(args.a, "diff_logs")
        eb, ub = parse_file(args.b, "diff_logs")
    except OSError as exc:
        print("diff_logs: cannot read: %s" % exc, file=sys.stderr)
        return 2

    ra = dma_region(ea)
    rb = dma_region(eb)
    base_a = args.base_a if args.base_a is not None else infer_base(ra)
    base_b = args.base_b if args.base_b is not None else infer_base(rb)
    if args.verbose:
        print(
            "N2DIFF A base=0x%08x (%s) B base=0x%08x (%s)"
            % (
                base_a,
                "given" if args.base_a is not None else "inferred",
                base_b,
                "given" if args.base_b is not None else "inferred",
            ),
            file=sys.stderr,
        )
    if ua or ub:
        print(
            "diff_logs: skipped unrecognised lines: A=%d B=%d" % (ua, ub),
            file=sys.stderr,
        )
    ta = any(e.op == "hwlog" and "truncated" in e.text for e in ea)
    tb = any(e.op == "hwlog" and "truncated" in e.text for e in eb)
    if ta or tb:
        print(
            "diff_logs: warning: log is truncated (A=%s B=%s); comparison is partial"
            % ("yes" if ta else "no", "yes" if tb else "no"),
            file=sys.stderr,
        )

    a = select(ea, ra, base_a, args.all)
    b = select(eb, rb, base_b, args.all)
    what = "operations" if args.all else "writes"
    d = compare(a, b)
    if d is None:
        print(
            "N2DIFF identical %s=%d (A total lines=%d, B total lines=%d)"
            % (what, len(a), len(ea), len(eb))
        )
        return 0

    i, la, lb = d
    print("N2DIFF first divergence in %s #%d" % (what, i + 1))
    if la is not None:
        print("  A line %d: %s" % (la.lineno, la.text))
    else:
        print("  A: <no such operation>")
    if lb is not None:
        print("  B line %d: %s" % (lb.lineno, lb.text))
    else:
        print("  B: <no such operation>")
    return 1


if __name__ == "__main__":
    sys.exit(main())
