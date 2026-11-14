#!/usr/bin/env python3
"""shaderstats.py -- per-function Valhall statistics of a metallib as the CURRENT translator compiles it.

Runs the y2s99b host harness (AIR -> NIR -> Mesa Valhall compiler, the same air_to_nir.c / air_metal.c that ship)
with BIFROST_MESA_DEBUG=shaders,shaderdb, then attributes every SHADER-DB line and the final Valhall disassembly
to the function whose summary line follows it (the harness prints the summary after the compile).

  python3 shaderstats.py HARNESS LIB.metallib OUT_PREFIX [--keep]

Writes OUT_PREFIX.tsv (one row per compiled stage) and prints a summary:
  - which unit bounds each fragment/compute shader (Mesa's own per-unit cycle model: fma/cvt/sfu/v/t/ls),
  - LD_BUFFER (UBO/SSBO loads through the load/store unit) per shader -- constant-buffer reads that were NOT pushed into FAU,
  - occupancy ('threads' 1 = more than 32 registers = half the warps in flight),
  - how the bound would change if every constant-offset LD_BUFFER of the shader were a pushed uniform (ls minus those loads).
The model is Mesa's (va_print_stats: 64 FMA, 64 CVT, 16 SFU, 16 V, 4 T, 1 LS per cycle); it is a static estimate, not a
measurement on the Mali.  Light job: one process per library, a few MB of RAM.
"""
import os, re, subprocess, sys

STAT = re.compile(r'SHADER-DB: (MESA_SHADER_\w+) shader: (\d+) inst, ([\d.]+) cycles, ([\d.]+) fma, ([\d.]+) cvt, ([\d.]+) sfu, '
                  r'([\d.]+) v, ([\d.]+) t, ([\d.]+) ls, (\d+) quadwords, (\d+) threads, (\d+) loops, (\d+):(\d+) spills:fills')
NAME = re.compile(r'^  (\S+)\s+(MESA_SHADER_\w+), (\d+) bytes of Valhall code')


def run(harness, lib, out):
    env = dict(os.environ, BIFROST_MESA_DEBUG='shaders,shaderdb')
    with open(out, 'w') as f:
        subprocess.run(['timeout', '600', 'nice', '-n', '10', 'stdbuf', '-oL', '-eL', harness, lib],
                       stdout=f, stderr=subprocess.STDOUT, env=env)


def parse(path):
    rows, stats, ldbuf, ldbuf_const, tex, insns, cur_dis = [], [], 0, 0, 0, 0, []
    in_final = False
    with open(path, errors='replace') as f:
        for line in f:
            m = STAT.search(line)
            if m:
                g = m.groups()
                stats.append(dict(stage=g[0], inst=int(g[1]), cycles=float(g[2]), fma=float(g[3]), cvt=float(g[4]), sfu=float(g[5]),
                                  v=float(g[6]), t=float(g[7]), ls=float(g[8]), qw=int(g[9]), threads=int(g[10]), loops=int(g[11]),
                                  spills=int(g[12]), fills=int(g[13]), ldbuf=ldbuf, ldbuf_const=ldbuf_const, tex=tex))
                ldbuf = ldbuf_const = tex = 0
                continue
            # final (packed) disassembly lines look like '   LD_BUFFER.i64.unsigned.slot0 @r2:r3, 0x0, r0'
            s = line.strip()
            if s.startswith('LD_BUFFER') and '@' in s:
                ldbuf += 1
                # offset operand constant (0x...) and index register: counts as constant-offset (pushable in principle)
                ops = s.split(',')
                if len(ops) >= 2 and re.match(r'\s*0x[0-9a-f]+', ops[1]):
                    ldbuf_const += 1
                elif re.search(r'@r\d+(:r\d+)*, \^?r\d+, \^?r\d+', s):
                    ldbuf_const += 1   # offset materialised by an IADD_IMM just before (as in CA's shaders)
            elif s.startswith('TEX') and '@' in s:
                tex += 1
            m = NAME.match(line)
            if m:
                fn, stage, nbytes = m.group(1), m.group(2), int(m.group(3))
                # the stats printed since the previous name belong to this function (vertex: position + varying)
                for st in stats:
                    st['function'] = fn
                    st['bytes'] = nbytes
                    rows.append(st)
                stats = []
    return rows


def bound(r, ls=None):
    ls = r['ls'] if ls is None else ls
    units = {'fma': r['fma'], 'cvt': r['cvt'], 'sfu': r['sfu'], 'v': r['v'], 't': r['t'], 'ls': ls}
    u = max(units, key=units.get)
    return u, units[u]


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    harness, lib, prefix = sys.argv[1:4]
    raw = prefix + '.raw'
    if not os.path.exists(raw):
        run(harness, lib, raw)
    rows = parse(raw)
    with open(prefix + '.tsv', 'w') as f:
        cols = ['function', 'stage', 'inst', 'cycles', 'fma', 'cvt', 'sfu', 'v', 't', 'ls', 'threads', 'spills', 'ldbuf', 'ldbuf_const', 'tex', 'bytes']
        f.write('\t'.join(cols) + '\tbound\tbound_if_pushed\n')
        for r in rows:
            b, bc = bound(r)
            pb, pbc = bound(r, max(0.0, r['ls'] - r['ldbuf_const']))
            f.write('\t'.join(str(r.get(c, '')) for c in cols) + '\t%s=%.2f\t%s=%.2f\n' % (b, bc, pb, pbc))
    frag = [r for r in rows if r['stage'] in ('MESA_SHADER_FRAGMENT', 'MESA_SHADER_COMPUTE')]
    if not frag:
        print('no fragment/compute stages parsed'); return 1
    nls = sum(1 for r in frag if bound(r)[0] == 'ls')
    sum_c = sum(r['cycles'] for r in frag)
    sum_p = sum(bound(r, max(0.0, r['ls'] - r['ldbuf_const']))[1] for r in frag)
    half = sum(1 for r in frag if r['threads'] == 1)
    withld = sum(1 for r in frag if r['ldbuf'])
    print('%s: %d fragment/compute stages; LS-bound %d (%.0f %%); with LD_BUFFER %d; half occupancy (>32 regs) %d; spilling %d' %
          (os.path.basename(lib), len(frag), nls, 100.0 * nls / len(frag), withld, half, sum(1 for r in frag if r['spills'])))
    print('  sum of modelled cycles %.1f -> %.1f if constant-buffer loads were pushed (x%.2f)' % (sum_c, sum_p, sum_c / max(sum_p, 1e-9)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
