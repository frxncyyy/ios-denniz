#!/usr/bin/env python3
"""Close the Mali variant space of a pipeline corpus, so a pack covers every variant a draw can ask for.

A cold variant compile costs 100-270 ms on the phone (2 ms on the host) and, with asynchronous pipelines,
the draw is skipped until it is done: that is the flicker and the lag when a new UI state opens.  The
variants of one pipeline differ only in the generic options of tegu_mali_compile_generic_pair_options:
  flags   DRAW_PARAMETERS 1 | SELECT_RT 2 | TILE_FETCH 4 | EXTENDED_VARYINGS 8 | THREAD_STORAGE 16 | RUNTIME_SPEC 64
  rt      the selected render target (word 2), only meaningful with SELECT_RT
Every variant seen in a device log is 0x19 (the build always sets 1|8|16) plus a subset of {2, 4, 64}.  This
tool adds the missing subsets (and rt 0..3 under SELECT_RT) to every pipeline that has at least one logged
variant, keeping its other fields (entry, msgcap, module sizes/hashes, vertex/fragment constants).

    expand_mali_variants.py corpus.txt corpus-expanded.txt
"""
import itertools
import re
import sys

BASE = 0x19
OPTIONAL = (2, 4, 64)
RT_MAX = 3


def opt_hex(flags, rt):
    return ''.join('%02x%02x%02x%02x' % (w & 255, w >> 8 & 255, w >> 16 & 255, w >> 24 & 255) for w in (1, flags, rt, 0))


def main():
    src, dst = sys.argv[1:3]
    lines = [l.rstrip('\n') for l in open(src)]
    groups = {}
    for l in lines:
        if not l.startswith('TPC_MISS v1 mali '):
            continue
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        shape = tuple(d.get(k, '-') for k in ('entry', 'rt', 'msgcap', 'vn', 'fn', 'vsha', 'fsha', 'vc', 'fc'))
        groups.setdefault(shape, d)
    have = {l for l in lines if l.startswith('TPC_MISS v1 mali ')}
    seen = set()
    for l in have:
        d = dict(re.findall(r'(\w+)=(\S+)', l))
        seen.add((tuple(d.get(k, '-') for k in ('entry', 'rt', 'msgcap', 'vn', 'fn', 'vsha', 'fsha', 'vc', 'fc')), d.get('opt', '-')))
    out = list(lines)
    added = 0
    for shape, d in groups.items():
        entry, rt, msgcap, vn, fn, vsha, fsha, vc, fc = shape
        for k in range(len(OPTIONAL) + 1):
            for sub in itertools.combinations(OPTIONAL, k):
                flags = BASE | sum(sub)
                for sel in (range(RT_MAX + 1) if flags & 2 else (0,)):
                    opt = opt_hex(flags, sel)
                    if (shape, opt) in seen:
                        continue
                    seen.add((shape, opt))
                    out.append('TPC_MISS v1 mali key=- entry=%s rt=%s msgcap=%s vn=%s fn=%s vsha=%s fsha=%s vc=%s fc=%s opt=%s' %
                               (entry, rt, msgcap, vn, fn, vsha, fsha, vc, fc, opt))
                    added += 1
    open(dst, 'w').write('\n'.join(out) + '\n')
    print('pipelines (shapes) %d, mali records %d -> %d (+%d)' % (len(groups), len(have), len(have) + added, added))


if __name__ == '__main__':
    main()
