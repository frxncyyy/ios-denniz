#!/usr/bin/env python3
"""Pipeline corpus for the TeguMetal pipeline pack: from hardware console logs
(and, once the device logs them, full replay records) to build-independent
pipeline descriptions, key verification and coverage.

Why a corpus.  Every cache key contains the build id, so a pack is valid for one
build.  What is stable across builds is the *description* of a pipeline: the two
functions (library SHA-256, entry name, function-constant values), the colour
targets and blends, the vertex descriptor, the translator profile and message
capacity.  This tool turns logs into those descriptions in the exact "TPC_MISS
v1 air" text grammar pipeline_cache_frontend.inc writes to misses.txt (and, per
TEGU_PSO_REC chunk, to the console); the host replay (--pipeline-replay mode of
render_frontend_host_test.m, driven by gpu/tools/build_pipeline_pack.py) builds
each description through the real frontend and compilers.

Sources, in order of fidelity
  record   TEGU_PSO_REC console chunks / misses.txt lines: everything, constants
           included (complete by construction).
  trace    TEGU_RENDER_INPUT/ATTACHMENT/BLEND/VERTEX_* lines (emitted on the
           generic-MRT fallback path only): descriptor without constants.  The
           constants of a function that declares none are known (empty
           specialisation); for functions with constants only captured sets
           (baseline and R100/R105 variants) can be tried, and the key prefix
           the device logged decides: a description is `verified` only when its
           recomputed 16-hex key prefix equals a logged miss key.
  miss     TEGU_PSO_MISS air only (names/variant/format/targets): not
           reproducible, reported as no_descriptor.

Nothing here substitutes zero constants silently: an unknown constant set stays
unknown and is listed.
"""
import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import pipeline_keys as pk  # noqa: E402

REPO_GPU = HERE.parents[1]
DEFAULT_FUNCTIONS = REPO_GPU / 'build/ui-corpus-20261025/functions.json'
DEFAULT_PROFILE = REPO_GPU / 'build/tegu-frontend/tegu-air-profile.h'
FIXED_ROWS = HERE / 'fixed_ui_frontend_host_test.inc'
FIXED_VARIANTS = HERE / 'check_fixed_ui_live_variants.py'
REC = re.compile(r'k=([0-9a-f]+) src=(\w+) i=(\d+)/(\d+) (.*)', re.S)
LINE = re.compile(r'(?:\|([\w.-]+)\| )?\b(TEGU_[A-Z_0-9]+)\b ?(.*)')


# ---------------------------------------------------------------- inputs ----
def profile_bytes(path: Path) -> bytes:
    data = path.read_bytes()
    if data.lstrip().startswith(b'{'):
        return data
    m = re.search(r'\{([^{}]*)\}', data.decode())
    return bytes(int(v) for v in m.group(1).split(',') if v.strip())


class Functions:
    """function name -> (library sha256 hex, declared constant count) from corpus functions.json."""
    def __init__(self, path=DEFAULT_FUNCTIONS):
        self.by_name = {}
        if Path(path).exists():
            for e in json.load(open(path))['entries']:
                self.by_name.setdefault(e['name'], {})[e['library_sha256']] = len(e.get('constants', []))

    def library(self, name):
        libs = self.by_name.get(name, {})
        return next(iter(libs)) if len(libs) == 1 else None

    def declared_constants(self, name):
        libs = self.by_name.get(name, {})
        return max(libs.values()) if libs else None


def fixed_constant_tables():
    """Captured fixed-UI constant sets: baseline vertex/fragment rows plus the R100/R105 fragment variants."""
    src = FIXED_ROWS.read_text()

    def rows(name):
        body = re.search(name + r'\[\]=\{(.*?)\n\};', src, re.S).group(1)
        out = []
        for m in re.finditer(r'\{"(\w+)",(\d+),(\d+),\{([^}]*)\}\}', body):
            size = int(m.group(3))
            out.append((m.group(1), int(m.group(2)), size, bytes(int(x, 16) for x in m.group(4).split(','))[:size]))
        return out
    variants = eval(re.search(r'VARIANTS = (\{.*?\n\})', FIXED_VARIANTS.read_text(), re.S).group(1))
    return rows('tegu_fixed_ui_vs_constants'), rows('tegu_fixed_ui_fs_constants'), variants


def constants_from_rows(rows, override=None):
    out = []
    for name, type_, size, data in rows:
        if override and name in override:
            data = bytes([override[name]]) + data[1:]
        out.append(pk.Constant(type_, data, name))
    return out


# ------------------------------------------------------------ descriptions --
class Pipeline:
    """One AIR pipeline description; the same fields the replay record carries."""
    def __init__(self, **kw):
        self.variant = kw['variant']
        self.format = kw['format']
        self.vertex, self.vlib, self.vspec = kw['vertex'], kw['vlib'], kw['vspec']
        self.fragment, self.flib, self.fspec = kw['fragment'], kw['flib'], kw['fspec']
        self.targets, self.blends = kw['targets'], kw['blends']
        self.attrs, self.layouts = kw['attrs'], kw['layouts']
        self.profile, self.msgcap = kw['profile'], kw['msgcap']
        self.source = kw.get('source', 'record')   # record | trace
        self.note = kw.get('note', '')

    def identity_of(self, which):
        lib = pk.library_identity(self.vlib if which == 'v' else self.flib)
        spec = self.vspec if which == 'v' else self.fspec
        if spec in (None, '-'):
            return lib
        entry, consts = pk.parse_replay(spec)
        return pk.specialized_identity(lib, entry, consts)

    def key(self, build, profile):
        return pk.air_key(build, self.variant, self.identity_of('v'), self.vertex, self.identity_of('f'), self.fragment,
                          self.format, self.targets, self.blends, self.attrs, self.layouts, profile, self.msgcap)

    def description_id(self):
        """Build-independent identity (dedupe across logs and builds)."""
        return hashlib.sha256(self.record('-').encode()).hexdigest()[:16]

    def record(self, key='-'):
        def rows(items, n):
            return ''.join('/'.join(str(v) for v in i) + ',' for i in items)
        return ('TPC_MISS v1 air key=%s variant=%u format=%u vertex=%s vlib=%s vspec=%s fragment=%s flib=%s fspec=%s '
                'targets=%s blends=%s attrs=%s layouts=%s profile=%s msgcap=%u') % (
            key, self.variant, self.format, self.vertex, self.vlib, self.vspec or '-', self.fragment, self.flib,
            self.fspec or '-', rows(self.targets, 2), rows(self.blends, 9), rows(self.attrs, 4), rows(self.layouts, 4),
            self.profile, self.msgcap)


def _tuples(text, width):
    out = []
    for item in [x for x in text.split(',') if x]:
        out.append(tuple(int(v) for v in item.split('/')))
        assert len(out[-1]) == width, (text, width)
    return out


def parse_record(text, source='record'):
    """Parse one "TPC_MISS v1 air" line (misses.txt or reassembled TEGU_PSO_REC)."""
    kv = dict(re.findall(r'(\w+)=(\S*)', text))
    return Pipeline(variant=int(kv['variant']), format=int(kv['format']), vertex=kv['vertex'], vlib=kv['vlib'],
                    vspec=None if kv['vspec'] == '-' else kv['vspec'], fragment=kv['fragment'], flib=kv['flib'],
                    fspec=None if kv['fspec'] == '-' else kv['fspec'], targets=_tuples(kv.get('targets', ''), 2),
                    blends=_tuples(kv.get('blends', ''), 9), attrs=_tuples(kv.get('attrs', ''), 4),
                    layouts=_tuples(kv.get('layouts', ''), 4), profile=kv['profile'], msgcap=int(kv['msgcap']),
                    source=source), kv.get('key', '-')


# ------------------------------------------------------------------ logs ----
class Log:
    """Events of one console log: per-process traces, AIR/Mali misses and TEGU_PSO_REC records."""
    def __init__(self, path):
        self.path = Path(path)
        self.traces, self.air, self.mali, self.records = [], [], [], []
        self.cache_lines = []
        trace, chunks, last = {}, {}, {}
        for line in open(path, errors='replace'):
            m = LINE.search(line)
            if not m:
                continue
            proc, tag, rest = m.groups()
            proc = proc or '-'
            kv = dict(re.findall(r'(\w+)=(\S+)', rest))
            if tag == 'TEGU_RENDER_INPUT':
                trace[proc] = last[proc] = {'proc': proc, 'vertex': kv['vertex'], 'fragment': kv['fragment'], 'cap': kv['capture'],
                                            'att': {}, 'attrs': [], 'layouts': [], 'blend': None, 'miss': None}
                self.traces.append(trace[proc])
            elif tag == 'TEGU_RENDER_ATTACHMENT' and proc in trace:
                trace[proc]['att'][int(kv['slot'])] = (int(kv['format'], 16), int(kv['mask'], 16))
            elif tag == 'TEGU_RENDER_VERTEX_ATTRIBUTE' and proc in trace:
                trace[proc]['attrs'].append((int(kv['slot'], 16), int(kv['format'], 16), int(kv['offset'], 16), int(kv['buffer'], 16)))
            elif tag == 'TEGU_RENDER_VERTEX_LAYOUT' and proc in trace:
                trace[proc]['layouts'].append((int(kv['slot'], 16), int(kv['stride'], 16), int(kv['step'], 16), int(kv['rate'], 16)))
            elif tag == 'TEGU_RENDER_BLEND' and proc in trace:
                trace[proc]['blend'] = {k: int(v, 16) for k, v in kv.items() if k != 'capture'}
            elif tag == 'TEGU_PSO_MISS':
                kind = rest.split()[0]
                e = {'kind': kind, 'proc': proc, **kv}
                if kind == 'air':
                    # The trace precedes its pipeline's build; pair by process, names and variant.
                    t = trace.get(proc)
                    if t and t['miss'] is None and kv['variant'] == '4' and t['vertex'] == kv['v'] and t['fragment'] == kv['f']:
                        t['miss'] = e
                    self.air.append(e)
                else:
                    self.mali.append(e)
            elif tag == 'TEGU_PSO_REC':
                h = REC.match(rest)
                if not h:
                    continue
                key, src, i, n, text = h.group(1), h.group(2), int(h.group(3)), int(h.group(4)), h.group(5).rstrip('\n').replace('#', ' ')
                slot = chunks.setdefault((proc, key), {'n': n, 'src': src, 'parts': {}})
                slot['parts'][i] = text
                if len(slot['parts']) == slot['n']:
                    body = ''.join(slot['parts'][j] for j in range(1, n + 1))
                    self.records.append({'proc': proc, 'k': key, 'src': slot['src'], 'text': body})
                    del chunks[(proc, key)]
            elif tag == 'TEGU_PIPELINE_CACHE':
                self.cache_lines.append(line.strip())
        self.incomplete_records = len(chunks)


def trace_to_pipeline(t, functions, profile_hex, msgcap=512):
    """Descriptor of the generic-MRT fallback (render_pipeline.inc, TEGU_PC_AIR_ATTACHMENT_BLENDS) from a trace.

    rtCount = highest bound slot (<4) + 1; an unbound slot compiles as format 80 mask 0; blend of slot 0 comes from
    TEGU_RENDER_BLEND, other slots are the default pass-through (the key decides whether that held)."""
    slots = [s for s in range(4) if t['att'].get(s, (0, 0))[0]]
    if not slots or t['blend'] is None:
        return None
    rt = max(slots) + 1
    targets = [((t['att'][s][0] or 80), (t['att'][s][1] if t['att'][s][0] else 0)) for s in range(rt)]
    b, blends = t['blend'], []
    for s in range(rt):
        if s == 0 and b['enabled']:
            blends.append((1, 1, b['rgbOp'], b['rgbSrc'], b['rgbDst'], b['alphaOp'], b['alphaSrc'], b['alphaDst'], targets[0][1]))
        else:
            blends.append((1, 0, 0, 1, 0, 0, 1, 0, targets[s][1]))
    vlib, flib = functions.library(t['vertex']), functions.library(t['fragment'])
    if not vlib or not flib:
        return None
    return dict(variant=4, format=0, vertex=t['vertex'], vlib=vlib, fragment=t['fragment'], flib=flib, targets=targets,
                blends=blends, attrs=t['attrs'], layouts=t['layouts'], profile=profile_hex, msgcap=msgcap)


def candidate_pipelines(base, functions, tables):
    """Constant candidates for one traced description: yields (label, Pipeline)."""
    vs_rows, fs_rows, variants = tables
    vdecl = functions.declared_constants(base['vertex'])
    fdecl = functions.declared_constants(base['fragment'])
    if not vdecl and not fdecl:
        yield 'no-constants', Pipeline(vspec=pk.specialized_replay(base['vertex'], []),
                                       fspec=pk.specialized_replay(base['fragment'], []), source='trace', **base)
        return
    if base['vertex'] == 'fixed_vert_lph_spc' and base['fragment'] == 'fixed_frag_lph_cph':
        vspec = pk.specialized_replay(base['vertex'], constants_from_rows(vs_rows))
        # R100/R105 captured fragment sets x attachment count.
        for label, override in [('baseline', {})] + [('serial%d' % k, v) for k, v in sorted(variants.items())]:
            for ac in (1, 2, 3):
                o = dict(override, fc_attachment_count=ac)
                yield 'fixed-%s-ac%d' % (label, ac), Pipeline(
                    vspec=vspec, fspec=pk.specialized_replay(base['fragment'], constants_from_rows(fs_rows, o)),
                    source='trace', **base)


def load_profile(path):
    data = profile_bytes(Path(path))
    return data, hashlib.sha256(data).hexdigest()


def analyse(log_paths, build, profile_path=DEFAULT_PROFILE, functions_path=DEFAULT_FUNCTIONS):
    """Per-log verdicts for every logged AIR miss plus the verified descriptions."""
    functions, tables = Functions(functions_path), fixed_constant_tables()
    profile, profile_hex = load_profile(profile_path)
    report, verified = [], {}
    for path in log_paths:
        log = Log(path)
        row = {'log': str(path), 'air_misses': len(log.air), 'mali_misses': len(log.mali), 'records': len(log.records),
               'verdicts': {}, 'unverified_records': 0}
        verdict = row['verdicts']
        logged = {e['key']: e for e in log.air}
        # 1. full records (device with TEGU_PSO_REC)
        for r in log.records:
            p, _ = parse_record(r['text'], 'record')
            if p.profile != profile_hex:
                row['unverified_records'] += 1
                verdict.setdefault(r['k'], 'profile_mismatch')   # --profile is not the bundle's translator profile
                continue
            k = p.key(build, profile).hex()
            if r['src'] == 'miss':
                verdict[r['k']] = 'verified' if k.startswith(r['k']) else 'record_key_mismatch'
            elif not k.startswith(r['k']):
                row['unverified_records'] += 1
            if k.startswith(r['k']):
                verified[p.description_id()] = p
        # 2. traces (legacy logs)
        for t in log.traces:
            miss = t['miss']
            if not miss or miss['key'] in verdict:
                continue
            base = trace_to_pipeline(t, functions, profile_hex)
            if base is None:
                verdict[miss['key']] = 'no_descriptor'
                continue
            hit = None
            tried = 0
            for label, p in candidate_pipelines(base, functions, tables):
                tried += 1
                if p.key(build, profile).hex().startswith(miss['key']):
                    hit = (label, p)
                    break
            if hit:
                p = hit[1]
                p.note = hit[0]
                verdict[miss['key']] = 'verified'
                verified[p.description_id()] = p
            elif tried:
                declared = max(functions.declared_constants(t['vertex']) or 0, functions.declared_constants(t['fragment']) or 0)
                verdict[miss['key']] = 'constants_unknown' if declared else 'key_mismatch'
            else:
                verdict[miss['key']] = 'constants_unknown'
        for k in logged:
            verdict.setdefault(k, 'no_descriptor')
        counts = {}
        for v in verdict.values():
            counts[v] = counts.get(v, 0) + 1
        row['air_summary'] = counts
        row['expected_names'] = {}
        report.append(row)
    return report, verified


def mali_coverage(log_paths, cache_dirs):
    """Share of logged Mali miss key prefixes produced by a replay (keys read from the cache entries)."""
    produced = {1: set(), 2: set()}
    for d in cache_dirs:
        for f in Path(d).rglob('k*-*.tpc'):
            h = f.read_bytes()[:96]
            if h[:4] == b'TGPC':
                kind = int.from_bytes(h[8:12], 'little')
                produced.setdefault(kind, set()).add(h[16:48].hex())
    out = []
    for path in log_paths:
        log = Log(path)
        for kind, name, rows in ((1, 'air', log.air), (2, 'mali', log.mali)):
            keys = {e['key'] for e in rows}
            got = {k for k in keys if any(p.startswith(k) for p in produced[kind])}
            out.append({'log': str(path), 'kind': name, 'logged': len(keys), 'reproduced': len(got), 'missing': sorted(keys - got)})
    return out


# ------------------------------------------------------------------- CLI ----
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('scan', help='verify descriptions against the key prefixes a log shows; write the replay corpus')
    s.add_argument('logs', nargs='+', type=Path)
    s.add_argument('--build', help='full build id of the bundle that produced the logs (nord2-arch9-<sha256>)')
    s.add_argument('--bundle', type=Path, help='Nord2Metal.bundle or its Mach-O: build id and translator profile are read from it')
    s.add_argument('--profile', type=Path, default=DEFAULT_PROFILE)
    s.add_argument('--functions', type=Path, default=DEFAULT_FUNCTIONS)
    s.add_argument('--out', type=Path, help='corpus file: one TPC_MISS v1 air record per verified description (merged, deduped)')
    s.add_argument('--json', type=Path, help='write the full report')
    c = sub.add_parser('mali', help='coverage of logged AIR/Mali key prefixes by a replay cache directory')
    c.add_argument('logs', nargs='+', type=Path)
    c.add_argument('--cache', type=Path, action='append', required=True)
    a = ap.parse_args()
    if a.cmd == 'scan':
        build = a.build
        if a.bundle:
            import make_pipeline_pack as mp
            build = mp.bundle_build_id(a.bundle)
        if not build:
            ap.error('--build or --bundle required')
        report, verified = analyse(a.logs, build, a.profile, a.functions)
        corpus = {}
        if a.out and a.out.exists():
            for line in a.out.read_text().splitlines():
                if line.startswith('TPC_MISS'):
                    p, _ = parse_record(line)
                    corpus[p.description_id()] = line
        for did, p in verified.items():
            corpus[did] = p.record()
        if a.out:
            a.out.write_text(''.join(corpus[k] + '\n' for k in sorted(corpus)))
        for row in report:
            print('%s: air misses %d, %s' % (row['log'], row['air_misses'], json.dumps(row['air_summary'], sort_keys=True)))
        print('verified descriptions: %d (corpus %d)' % (len(verified), len(corpus)))
        if a.json:
            a.json.write_text(json.dumps({'build': build, 'report': report}, indent=1))
    else:
        for row in mali_coverage(a.logs, a.cache):
            print('%s %s: %d/%d reproduced%s' % (row['log'], row['kind'], row['reproduced'], row['logged'],
                                              '' if not row['missing'] else ' (missing %d)' % len(row['missing'])))


if __name__ == '__main__':
    main()
