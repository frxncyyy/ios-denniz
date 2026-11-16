#!/usr/bin/env python3
"""Robust parser for Nord2 USB console logs (xnu-live.log), text only.

The console cuts lines at ~256 bytes and glues records together.  Every
physical line is split into segments at '|proc|' tags and at 'NORD2 ' kernel
prefixes.  A segment that was interrupted (not the last on its line, or the
last on a >=250-byte line) loses its final key=value, which may be cut.

Clock: SpringBoard NSLog stamps 'HH:MM:SS.-mmm' (the fraction is printed as
ms-1000; seconds are exact) are the only wall clock that spans the run; each
record gets a time interpolated by line number between the nearest stamps.

Usage as a module: events(path) -> list of dicts; clock(events) adds 't'.
"""
import re
import pathlib

SPLIT = re.compile(r'(?=\|[A-Za-z][A-Za-z0-9._-]*\|)|(?=NORD2 [A-Z])|(?=NORD2-[A-Z])')
PROC = re.compile(r'^\|([A-Za-z][A-Za-z0-9._-]*)\|\s?')
KV = re.compile(r'(?<![A-Za-z0-9_])([A-Za-z_][A-Za-z0-9_]*)=(-?\d+)(?:/(-?\d+))?(?:/(-?\d+))?(?:/(-?\d+))?(?:/(-?\d+))?(?=([ \n|,~:]|$))')
SBTIME = re.compile(r'(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d)\.(-?\d+) SpringBoard\[')
TYPE = re.compile(r'(TEGU_[A-Z_]+\+?|NORD2 [A-Z]+(?: [A-Z]+)?(?=[: ])|SpringBoard\[)')


def _kvs(body, drop_last):
    out = {}
    found = list(KV.finditer(body))
    if drop_last and found:
        found = found[:-1]
    for m in found:
        k = m.group(1)
        vals = [int(x) for x in m.group(2, 3, 4, 5, 6) if x is not None]
        if k in out:
            continue
        out[k] = vals[0] if len(vals) == 1 else tuple(vals)
    return out


def events(path):
    text = pathlib.Path(path).read_bytes().decode('latin1')
    evs = []
    for ln, line in enumerate(text.split('\n'), 1):
        segs = [s for s in SPLIT.split(line) if s]
        for i, seg in enumerate(segs):
            pm = PROC.match(seg)
            proc = pm.group(1) if pm else None
            body = seg[pm.end():] if pm else seg
            tm = TYPE.search(body[:40])
            typ = tm.group(1) if tm else None
            interrupted = (i < len(segs) - 1) or len(line) >= 250
            ev = {'line': ln, 'proc': proc, 'type': typ, 'text': body, 'cut': interrupted}
            if typ == 'SpringBoard[':
                m = SBTIME.search(body)
                if m:
                    h, mi, s, frac = int(m.group(4)), int(m.group(5)), int(m.group(6)), int(m.group(7))
                    ms = frac + 1000 if frac < 0 else frac
                    ev['sbt'] = h * 3600 + mi * 60 + s + ms / 1000.0
            elif typ:
                ev['kv'] = _kvs(body, interrupted)
            evs.append(ev)
    return evs


def clock(evs):
    """Attach 't' (s, SpringBoard clock) by line interpolation; None outside the stamps."""
    anchors = []
    for e in evs:
        if 'sbt' in e:
            if anchors and e['sbt'] < anchors[-1][1] - 0.5:
                continue  # non-monotonic stamp (glued/garbled)
            anchors.append((e['line'], e['sbt']))
    if not anchors:
        return evs
    t0 = anchors[0][1]
    j = 0
    for e in evs:
        ln = e['line']
        while j + 1 < len(anchors) and anchors[j + 1][0] <= ln:
            j += 1
        if ln < anchors[0][0] or ln > anchors[-1][0]:
            e['t'] = None
            continue
        a = anchors[j]
        b = anchors[j + 1] if j + 1 < len(anchors) else a
        if b[0] == a[0]:
            e['t'] = a[1] - t0
        else:
            e['t'] = a[1] - t0 + (b[1] - a[1]) * (ln - a[0]) / (b[0] - a[0])
    return evs


def first(v):
    return v[0] if isinstance(v, tuple) else v


def second(v):
    return v[1] if isinstance(v, tuple) and len(v) > 1 else None
