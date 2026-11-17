#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Turn the probe's JSONL files into the stage-0 answer sheet (WP10).

Usage:
  parse_probe.py probe-1.jsonl [probe-2.jsonl ...] [--baseline old.jsonl ...]

WP07's runner writes one JSON object per line:
  header : {"probe": "...", "kind": "header", ...}
  test   : {"test": "vt.decode.avc1", "result": "pass|fail|skip|timeout|crash",
            "signal": 0, "ms": 12, "fields": {"key": "value", ...}}
  end    : {"kind": "end", ...}

The output is a table of every test and its fields, the encoders that
VideoToolbox reported, one verdict line per PLAN.md stage-0 question, and,
with --baseline, what changed between two runs.  A truncated last line is
tolerated and reported; a malformed line anywhere else is reported and
skipped.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys


class Record:
    __slots__ = ("name", "result", "signal", "ms", "fields", "file")

    def __init__(self, name, result, signal, ms, fields, file):
        self.name = name
        self.result = result
        self.signal = signal
        self.ms = ms
        self.fields = fields
        self.file = file


def read_jsonl(path, warn):
    """Return (records, stats).  Tolerates a truncated last line."""
    recs = []
    stats = {"lines": 0, "tests": 0, "truncated": 0, "malformed": 0,
             "header": 0, "end": 0}
    try:
        lines = open(path, "r", encoding="utf-8", errors="replace").read().splitlines()
    except OSError as e:
        warn.append("%s: cannot read: %s" % (path, e))
        return recs, stats
    for i, line in enumerate(lines, 1):
        if not line.strip():
            continue
        stats["lines"] += 1
        try:
            obj = json.loads(line)
        except json.JSONDecodeError as e:
            if i == len(lines):
                stats["truncated"] += 1
                warn.append("%s:%d: truncated last line ignored (%s)" % (path, i, e.msg))
            else:
                stats["malformed"] += 1
                warn.append("%s:%d: malformed JSON ignored (%s)" % (path, i, e.msg))
            continue
        if not isinstance(obj, dict):
            stats["malformed"] += 1
            warn.append("%s:%d: not a JSON object" % (path, i))
            continue
        kind = obj.get("kind")
        if kind == "header" or ("test" not in obj and "probe" in obj):
            stats["header"] += 1
            continue
        if kind == "end" or "test" not in obj:
            stats["end"] += 1
            continue
        fields = obj.get("fields")
        if not isinstance(fields, dict):
            fields = {}
        recs.append(Record(str(obj["test"]), str(obj.get("result", "unknown")),
                           obj.get("signal", 0), obj.get("ms", 0), fields, path))
        stats["tests"] += 1
    return recs, stats


def fmt_fields(fields, limit=240):
    parts = []
    for k in sorted(fields):
        v = fields[k]
        if isinstance(v, (dict, list)):
            v = json.dumps(v, sort_keys=True, separators=(",", ":"))
        else:
            v = str(v)
        parts.append("%s=%s" % (k, v))
    s = " ".join(parts)
    if len(s) > limit:
        s = s[:limit - 3] + "..."
    return s


def last_by_name(recs):
    out = {}
    for r in recs:
        out[r.name] = r
    return out


def find(last, *prefixes):
    """Last record whose name equals or starts with one of the prefixes."""
    best = None
    for name, r in last.items():
        for p in prefixes:
            if name == p or name.startswith(p + "."):
                best = r
                break
    return best


def verdict_line(label, rec, extra=""):
    if rec is None:
        return "VERDICT %-12s MISSING (no test reported)" % (label + ":")
    if rec.result == "pass":
        ans = "yes"
    elif rec.result == "skip":
        ans = "SKIP"
    else:
        ans = "no"
    why = " (%s %s" % (rec.name, rec.result)
    if rec.signal:
        why += " signal=%s" % rec.signal
    if extra:
        why += " " + extra
    why += ")"
    return "VERDICT %s %s%s" % (label + ":", ans, why)


def encoder_values(rec):
    if rec is None:
        return []
    vals = []
    for k in sorted(rec.fields, key=lambda s: (len(s), s)):
        kl = k.lower()
        # WP07's T01 uses e0, e1, ... plus status/count/list.
        if not (re.fullmatch(r"e\d+", k) or "encoder" in kl or "codec" in kl):
            continue
        v = rec.fields[k]
        if isinstance(v, list):
            vals.extend(str(x) for x in v)
        elif isinstance(v, dict):
            vals.append(json.dumps(v, sort_keys=True))
        else:
            vals.append(str(v))
    return vals


def find_encode(last, codec):
    """A real encode test (vt.encode.<codec>.<W>x<H>), not the req-hw probes.

    Returns the first passing size if there is one, else the last reported.
    """
    pat = re.compile(r"^vt\.encode\.%s\.\d+x\d+$" % re.escape(codec))
    cands = [(n, r) for n, r in last.items() if pat.match(n)]
    if not cands:
        return None
    for _n, r in sorted(cands):
        if r.result == "pass":
            return r
    return sorted(cands)[-1][1]


def diff(current, baseline):
    out = []
    for name in sorted(set(current) | set(baseline)):
        a, b = baseline.get(name), current.get(name)
        if a is None:
            out.append("only in current: %s (%s)" % (name, b.result))
        elif b is None:
            out.append("only in baseline: %s (%s)" % (name, a.result))
        else:
            changes = []
            if a.result != b.result:
                changes.append("result %s -> %s" % (a.result, b.result))
            for k in sorted(set(a.fields) | set(b.fields)):
                av, bv = a.fields.get(k, "<absent>"), b.fields.get(k, "<absent>")
                if av != bv:
                    changes.append("fields.%s %r -> %r" % (k, av, bv))
            if changes:
                out.append("%s: %s" % (name, "; ".join(changes)))
    return out


def main(argv):
    ap = argparse.ArgumentParser(description="probe JSONL report (WP10)")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--baseline", action="append", default=[],
                    help="an older probe JSONL to diff against (repeatable)")
    args = ap.parse_args(argv)

    warn = []
    all_recs = []
    stats = {"files": 0, "tests": 0, "truncated": 0, "malformed": 0}
    for path in args.files:
        if not os.path.exists(path):
            print("ORACLE probe FAIL: %s does not exist" % path)
            return 1
        recs, st = read_jsonl(path, warn)
        stats["files"] += 1
        stats["tests"] += st["tests"]
        stats["truncated"] += st["truncated"]
        stats["malformed"] += st["malformed"]
        all_recs.extend(recs)
        print("PROBE FILE %s lines=%d tests=%d truncated=%d malformed=%d"
              % (path, st["lines"], st["tests"], st["truncated"], st["malformed"]))

    for w in warn:
        print("WARN " + w)

    print("")
    print("TEST TABLE")
    print("%-34s %-8s %8s  %s" % ("test", "result", "ms", "fields"))
    for r in all_recs:
        print("%-34s %-8s %8s  %s" % (r.name[:34], r.result, r.ms, fmt_fields(r.fields)))

    last = last_by_name(all_recs)
    print("")
    print("ENCODERS")
    enc = find(last, "vt.encoders")
    vals = encoder_values(enc)
    if vals:
        for v in vals:
            print("  " + v)
    else:
        print("  (none reported)")

    print("")
    print("STAGE-0 VERDICTS")
    print(verdict_line("decode avc1", find(last, "vt.decode.avc1")))
    print(verdict_line("decode hvc1", find(last, "vt.decode.hvc1")))
    print(verdict_line("encode avc1", find_encode(last, "avc1")))
    print(verdict_line("encode hvc1", find_encode(last, "hvc1")))
    jpeg = find_encode(last, "jpeg")
    if jpeg is None:
        jpeg = find(last, "img.write/read.jpeg")
    print(verdict_line("jpeg", jpeg))
    print(verdict_line("heic write", find(last, "img.write/read.heic")))
    print(verdict_line("aac", find(last, "audio.aac")))

    counts = {}
    for r in all_recs:
        counts[r.result] = counts.get(r.result, 0) + 1
    print("")
    print("PROBE SUMMARY tests=%d pass=%d fail=%d skip=%d timeout=%d crash=%d other=%d"
          % (stats["tests"], counts.get("pass", 0), counts.get("fail", 0),
             counts.get("skip", 0), counts.get("timeout", 0),
             counts.get("crash", 0),
             sum(v for k, v in counts.items()
                 if k not in ("pass", "fail", "skip", "timeout", "crash"))))

    if args.baseline:
        base_recs = []
        for path in args.baseline:
            if not os.path.exists(path):
                print("ORACLE probe FAIL: baseline %s does not exist" % path)
                return 1
            recs, _st = read_jsonl(path, warn)
            base_recs.extend(recs)
        changes = diff(last, last_by_name(base_recs))
        print("")
        print("DIFF vs baseline")
        if changes:
            for c in changes:
                print("  " + c)
        else:
            print("  none")

    print("ORACLE probe PASS files=%d tests=%d truncated=%d"
          % (stats["files"], stats["tests"], stats["truncated"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
