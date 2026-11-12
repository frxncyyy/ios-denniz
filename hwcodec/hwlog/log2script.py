#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Convert an X01 n2vc_hw op log into a T02 fake-hardware script and a register summary.

Input is the log written by hwlog/n2vc_hw_log.c.  Raw T02 recorder lines
(without the timestamp prefix) are accepted too, so a PC fake run can be
converted as well.

Script output (T02 grammar, tests/hw/README.md):
  * every read whose logged value the register array would not produce is
    emitted as a one-shot `read WIN OFF returns VALUE once` rule, in order;
  * every successful `IRQ wait` emits `irq N status S` and an
    `irq N fires K ms after write WIN OFF VALUE[&MASK]` rule whose delay is
    derived from the write and wait timestamps;
  * nothing is emitted for writes, power, DMA or `IRQ clear`: the replayed
    program performs those calls itself.

Summary output: one row per register with window, offset, write/read/poll
counts, first and last value written, the masks used and the distinct values
read.
"""

import argparse
import re
import sys
from collections import OrderedDict
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

WINDOWS = [
    "VENC",
    "VENC_C1",
    "VDEC_SYS",
    "VDEC_BASE",
    "VDEC_VLD",
    "VDEC_MC",
    "VDEC_MV",
    "VDEC_MISC",
    "VDEC_LAT_MISC",
    "VDEC_LAT_VLD",
    "VDEC_RACING",
    "VDEC_SOC_GCON",
]
WIN_INDEX = {name: i for i, name in enumerate(WINDOWS)}
WIN_BYTES = {
    "VENC": 0x3000,       # K02 rev 3: vpud maps 0x3000; JPGENC is at +0x10000
    "VENC_C1": 0x3000,
    "VDEC_SYS": 0x1000,
    "VDEC_BASE": 0x1000,
    "VDEC_VLD": 0x1000,
    "VDEC_MC": 0x1000,
    "VDEC_MV": 0x1000,
    "VDEC_MISC": 0x4000,
    "VDEC_LAT_MISC": 0x1000,
    "VDEC_LAT_VLD": 0x400,
    "VDEC_RACING": 0x1000,
    "VDEC_SOC_GCON": 0x1000,
}
ERRNO = {
    "ETIMEDOUT": 110,
    "EINVAL": 22,
    "EACCES": 13,
    "ENOMEM": 12,
    "ENOENT": 2,
    "EIO": 5,
    "ENOSPC": 28,
    "ELOOP": 40,
}


@dataclass
class Entry:
    lineno: int
    text: str
    ts: Optional[int]
    op: str
    block: Optional[str] = None
    on: Optional[int] = None
    win: Optional[str] = None
    off: Optional[int] = None
    value: Optional[int] = None
    mask: Optional[int] = None
    rc: Optional[int] = None
    irq: Optional[int] = None
    timeout_ms: Optional[int] = None
    status: Optional[int] = None
    handle: Optional[int] = None
    pa: Optional[int] = None
    flags: Optional[int] = None
    direction: Optional[str] = None
    crc: Optional[int] = None
    ok: Optional[bool] = None
    fields: Dict[str, str] = field(default_factory=dict)


def num(tok: str) -> int:
    """Parse a decimal or 0x... number exactly (no Python octals)."""
    if tok is None:
        raise ValueError("missing number")
    if tok.lower().startswith(("0x", "-0x", "+0x")):
        return int(tok, 16)
    if tok in ("-", "?"):
        raise ValueError("no value")
    return int(tok, 10)


def valid_reg(win: Optional[str], off: Optional[int]) -> bool:
    if win is None or off is None or win not in WIN_BYTES:
        return False
    return off >= 0 and off % 4 == 0 and off <= WIN_BYTES[win] - 4


def canon_win(tok: str) -> str:
    up = tok.upper()
    if up in WIN_INDEX:
        return up
    try:
        i = num(tok)
    except ValueError:
        return tok
    if 0 <= i < len(WINDOWS):
        return WINDOWS[i]
    return tok


def parse_rc(tok: Optional[str]) -> Optional[int]:
    if tok is None:
        return None
    if tok in ("ok", "0"):
        return 0
    if tok == "timeout":
        return -ERRNO["ETIMEDOUT"]
    if tok.startswith("-") and tok[1:] in ERRNO:
        return -ERRNO[tok[1:]]
    try:
        return num(tok)
    except ValueError:
        return None


def split_arrow(tokens: List[str]) -> Tuple[List[str], Optional[int]]:
    """Return (tokens before '->', rc after it). Missing arrow means rc=0."""
    try:
        i = tokens.index("->")
    except ValueError:
        return tokens, 0
    rc = parse_rc(tokens[i + 1]) if i + 1 < len(tokens) else None
    return tokens[:i], rc


def kv(tokens: List[str]) -> Dict[str, str]:
    out: Dict[str, str] = {}
    for t in tokens:
        if "=" in t:
            k, v = t.split("=", 1)
            out[k] = v
    return out


TS_RE = re.compile(r"^(\d+)\s+(.*)$")


def parse_line(text: str, lineno: int) -> Optional[Entry]:
    s = text.rstrip("\r\n")
    if not s.strip() or s.lstrip().startswith("#"):
        return None
    m = TS_RE.match(s)
    if m:
        ts: Optional[int] = int(m.group(1))
        body = m.group(2)
    else:
        ts = None
        body = s
    tok = body.split()
    if not tok:
        return None
    head, rc = split_arrow(tok)
    e = Entry(lineno=lineno, text=s, ts=ts, op="unknown", rc=rc)

    if head[0] == "POWER" and len(head) >= 3:
        e.op = "power"
        e.block = head[1]
        e.on = 1 if head[2].lower() in ("on", "1") else 0
    elif head[0] == "W" and len(head) >= 4:
        e.op = "w"
        e.win = canon_win(head[1])
        e.off = num(head[2])
        try:
            e.value = num(head[3])
        except ValueError:
            e.value = None
        if len(head) >= 6 and head[4] == "mask":
            e.mask = num(head[5])
        else:
            e.mask = 0xFFFFFFFF
    elif head[0] == "R" and len(head) >= 4:
        e.op = "r"
        e.win = canon_win(head[1])
        e.off = num(head[2])
        try:
            e.value = num(head[3])
        except ValueError:
            e.value = None
            if rc is None or rc == 0:
                e.rc = -ERRNO["EACCES"]
        if len(head) >= 5 and head[4] == "script":
            e.fields["script"] = "1"
    elif head[0] == "POLL" and len(head) >= 6 and head[4] == "mask":
        e.op = "poll"
        e.win = canon_win(head[1])
        e.off = num(head[2])
        try:
            e.value = num(head[3])
        except ValueError:
            e.value = None
        e.mask = num(head[5])
        tail = tok[tok.index("->") + 2 :] if "->" in tok else []
        e.ok = (rc == 0)
        for t in tail:
            if t.startswith("reads="):
                e.fields["reads"] = t.split("=", 1)[1]
    elif head[0] == "DELAY" and len(head) >= 3 and head[2] == "us":
        e.op = "delay"
        e.value = num(head[1])
    elif head[0] == "IRQ" and len(head) >= 3 and head[1] == "wait":
        e.op = "irq_wait"
        e.irq = num(head[2])
        f = kv(head[3:])
        if "timeout_ms" in f:
            e.timeout_ms = num(f["timeout_ms"])
        tail = tok[tok.index("->") + 2 :] if "->" in tok else []
        tf = kv(tail)
        if "status" in tf:
            e.status = None if tf["status"] == "none" else num(tf["status"])
    elif head[0] == "IRQ" and len(head) >= 3 and head[1] == "clear":
        e.op = "irq_clear"
        e.irq = num(head[2])
    elif head[0] == "DMA" and len(head) >= 2 and head[1] == "alloc":
        e.op = "dma_alloc"
        f = kv(head[2:])
        if "handle" in f:
            e.handle = num(f["handle"])
        if "pa" in f:
            e.pa = num(f["pa"])
        if "bytes" in f:
            e.fields["bytes"] = f["bytes"]
        if "flags" in f:
            e.flags = num(f["flags"])
    elif head[0] == "DMA" and len(head) >= 2 and head[1] == "free":
        e.op = "dma_free"
        f = kv(head[2:])
        if "handle" in f:
            e.handle = num(f["handle"])
    elif head[0] == "DMA" and len(head) >= 4 and head[1] == "rw":
        e.op = "dma_rw"
        f = kv(head[2:])
        if "handle" in f:
            e.handle = num(f["handle"])
        if "off" in f:
            e.off = num(f["off"])
        if "bytes" in f:
            e.fields["bytes"] = f["bytes"]
        if "crc32" in f:
            e.crc = num(f["crc32"])
        for t in head[2:]:
            if t in ("W", "R"):
                e.direction = t
    elif head[0] == "SCRIPT":
        e.op = "script"
    elif head[0] == "HWLOG":
        e.op = "hwlog"
    else:
        e.op = "unknown"
    return e


def parse_file(path: str, prog: str = "log2script"):
    if path == "-":
        fh = sys.stdin
        close = False
    else:
        fh = open(path, "r", encoding="utf-8", errors="replace")
        close = True
    entries: List[Entry] = []
    unknown = 0
    try:
        for lineno, line in enumerate(fh, 1):
            try:
                e = parse_line(line, lineno)
            except (ValueError, IndexError) as exc:
                unknown += 1
                if unknown <= 20:
                    print(
                        "%s: line %d: cannot parse (%s): %s"
                        % (prog, lineno, exc, line.rstrip()),
                        file=sys.stderr,
                    )
                continue
            if e is None:
                continue
            if e.op == "unknown":
                unknown += 1
                if unknown <= 20:
                    print(
                        "%s: line %d: unrecognised: %s"
                        % (prog, lineno, line.rstrip()),
                        file=sys.stderr,
                    )
                continue
            entries.append(e)
    finally:
        if close:
            fh.close()
    return entries, unknown


class Builder:
    def __init__(self, max_read_values: int = 16):
        self.max_read_values = max_read_values
        self.read_rules: List[str] = []
        self.irq_status: Dict[int, int] = OrderedDict()
        self.irq_rules: List[str] = []
        self.irq_seen = set()
        self.shadow: Dict[Tuple[str, int], int] = {}
        self.prev_write: Optional[Tuple[Optional[int], str, int, Optional[int], int]] = None
        self.summary: "OrderedDict[Tuple[str, int], dict]" = OrderedDict()
        self.warnings: List[str] = []

    def rec(self, win: str, off: int) -> dict:
        key = (win, off)
        if key not in self.summary:
            self.summary[key] = {
                "writes": 0,
                "reads": 0,
                "polls": 0,
                "first": None,
                "last": None,
                "masks": [],
                "read_values": [],
            }
        return self.summary[key]

    def feed(self, e: Entry) -> None:
        if e.op == "w":
            if e.win is None or e.off is None or e.value is None:
                return
            r = self.rec(e.win, e.off)
            r["writes"] += 1
            if r["first"] is None:
                r["first"] = e.value
            r["last"] = e.value
            mask = e.mask if e.mask is not None else 0xFFFFFFFF
            if mask not in r["masks"]:
                r["masks"].append(mask)
            if not valid_reg(e.win, e.off):
                self.warnings.append(
                    "line %d: write outside the register windows ignored for replay"
                    % e.lineno
                )
                return
            key = (e.win, e.off)
            old = self.shadow.get(key, 0)
            self.shadow[key] = (old & ~mask) | (e.value & mask)
            self.prev_write = (e.ts, e.win, e.off, e.value, mask)
        elif e.op == "r":
            if e.win is None or e.off is None:
                return
            r = self.rec(e.win, e.off)
            r["reads"] += 1
            if not valid_reg(e.win, e.off):
                self.warnings.append(
                    "line %d: read outside the register windows ignored for replay"
                    % e.lineno
                )
                return
            if e.value is None:
                return
            if e.value not in r["read_values"]:
                r["read_values"].append(e.value)
            if e.value != self.shadow.get((e.win, e.off), 0):
                self.read_rules.append(
                    "read %s 0x%04x returns 0x%08x once"
                    % (e.win, e.off, e.value)
                )
        elif e.op == "poll":
            if e.win is None or e.off is None:
                return
            r = self.rec(e.win, e.off)
            r["polls"] += 1
            if not valid_reg(e.win, e.off):
                self.warnings.append(
                    "line %d: poll outside the register windows ignored for replay"
                    % e.lineno
                )
                return
            # The POLL line carries the trigger value, not the observed value.
            # On success the read matched it in the mask; on timeout the read
            # value is unknown and must not become a read override.
            if not e.ok or e.value is None:
                cur = self.shadow.get((e.win, e.off), 0)
                if e.value is not None and (cur & (e.mask or 0xFFFFFFFF)) == (
                    e.value & (e.mask or 0xFFFFFFFF)
                ):
                    self.warnings.append(
                        "line %d: POLL timed out but the replayed register value "
                        "already matches; the fake will succeed"
                        % e.lineno
                    )
                return
            if e.value not in r["read_values"]:
                r["read_values"].append(e.value)
            if e.value != self.shadow.get((e.win, e.off), 0):
                self.read_rules.append(
                    "read %s 0x%04x returns 0x%08x once"
                    % (e.win, e.off, e.value)
                )
        elif e.op == "irq_wait":
            if e.irq is None:
                return
            status = e.status if e.status is not None else 1
            self.irq_status[e.irq] = status
            if e.rc != 0:
                return
            if self.prev_write is None:
                self.warnings.append(
                    "line %d: irq %d wait succeeded with no preceding write; no trigger rule"
                    % (e.lineno, e.irq)
                )
                return
            wts, win, off, value, mask = self.prev_write
            if value is None or not valid_reg(win, off):
                self.warnings.append(
                    "line %d: irq %d has no valid trigger register; no trigger rule"
                    % (e.lineno, e.irq)
                )
                return
            if e.ts is not None and wts is not None:
                delay_ms = max(0, (e.ts - wts) // 1000)
            else:
                delay_ms = 0
            if mask == 0xFFFFFFFF:
                trigger = "0x%08x" % (value & 0xFFFFFFFF)
                key = (e.irq, win, off, value & 0xFFFFFFFF)
            else:
                trig = value & mask
                trigger = "0x%08x&0x%08x" % (trig, mask)
                key = (e.irq, win, off, trig, mask)
            if key in self.irq_seen:
                return
            self.irq_seen.add(key)
            self.irq_rules.append(
                "irq %d fires %d ms after write %s 0x%04x %s"
                % (e.irq, delay_ms, win, off, trigger)
            )

    def script_lines(self, source: str) -> List[str]:
        out = [
            "# n2vc_hw fake-hardware script generated by log2script.py",
            "# source: %s" % source,
            "# read overrides reproduce the logged read values in order.",
        ]
        out.extend(self.read_rules)
        for irq in sorted(self.irq_status):
            out.append("irq %d status 0x%08x" % (irq, self.irq_status[irq]))
        out.extend(self.irq_rules)
        return out

    def summary_lines(self, source: str) -> List[str]:
        out = [
            "# X01 register summary from %s" % source,
            "# window offset writes reads polls first_write last_write masks read_values",
        ]

        def order(key):
            win, off = key
            return (WIN_INDEX.get(win, len(WINDOWS)), off)

        for win, off in sorted(self.summary, key=order):
            r = self.summary[(win, off)]
            masks = ",".join("0x%08x" % m for m in r["masks"]) or "-"
            vals = ["0x%08x" % v for v in r["read_values"]]
            if len(vals) > self.max_read_values:
                vals = vals[: self.max_read_values] + ["..."]
            reads = ",".join(vals) or "-"
            first = "0x%08x" % r["first"] if r["first"] is not None else "-"
            last = "0x%08x" % r["last"] if r["last"] is not None else "-"
            out.append(
                "%s 0x%04x writes=%d reads=%d polls=%d first=%s last=%s masks=%s read_values=%s"
                % (
                    win,
                    off,
                    r["writes"],
                    r["reads"],
                    r["polls"],
                    first,
                    last,
                    masks,
                    reads,
                )
            )
        return out


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser(
        description="convert an X01 n2vc_hw op log to a T02 fake script and a register summary"
    )
    ap.add_argument("log", help="input log ('-' = stdin)")
    ap.add_argument("-o", "--script", default="-", help="script output ('-' = stdout)")
    ap.add_argument("--summary", help="summary output (default: do not write one)")
    ap.add_argument(
        "--max-read-values",
        type=int,
        default=16,
        help="distinct read values per register in the summary (default 16)",
    )
    ap.add_argument("--quiet", action="store_true", help="no stderr warnings")
    args = ap.parse_args(argv)

    if args.script == "-" and args.summary == "-":
        print("log2script: --script and --summary cannot both be stdout", file=sys.stderr)
        return 2
    try:
        entries, unknown = parse_file(args.log)
    except OSError as exc:
        print("log2script: cannot read %s: %s" % (args.log, exc), file=sys.stderr)
        return 2

    b = Builder(max_read_values=args.max_read_values)
    for e in entries:
        b.feed(e)

    script = b.script_lines(args.log)
    if args.script == "-":
        sys.stdout.write("\n".join(script) + "\n")
    else:
        try:
            with open(args.script, "w", encoding="utf-8") as fh:
                fh.write("\n".join(script) + "\n")
        except OSError as exc:
            print("log2script: cannot write %s: %s" % (args.script, exc), file=sys.stderr)
            return 2

    if args.summary:
        summary = b.summary_lines(args.log)
        try:
            with open(args.summary, "w", encoding="utf-8") as fh:
                fh.write("\n".join(summary) + "\n")
        except OSError as exc:
            print("log2script: cannot write %s: %s" % (args.summary, exc), file=sys.stderr)
            return 2

    if not args.quiet:
        for w in b.warnings:
            print("log2script: warning: %s" % w, file=sys.stderr)
        if unknown:
            print(
                "log2script: %d unrecognised line(s) skipped" % unknown,
                file=sys.stderr,
            )
    if not entries:
        print("log2script: no n2vc_hw operations in %s" % args.log, file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
