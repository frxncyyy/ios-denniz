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
import collections
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
GPU_BUILD = REPO_GPU / 'build'
FIXED_ROWS = HERE / 'fixed_ui_frontend_host_test.inc'
FIXED_VARIANTS = HERE / 'check_fixed_ui_live_variants.py'
# The USB console drops the newline between back-to-back writes of one process: several
# TEGU_PSO_REC chunks (each with its own '|proc| ' prefix) can share one physical line.
GLUED = re.compile(r'(?=\|[\w.-]+\| TEGU_PSO_)')
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
        self.plan = kw.get('plan', 'm2v')          # m2v | air (the record's plan=air: AIR-derived planner metadata)
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
                          self.format, self.targets, self.blends, self.attrs, self.layouts, profile, self.msgcap, self.plan)

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
            self.profile, self.msgcap) + (' plan=air' if self.plan == 'air' else '')


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
                    source=source, plan=kv.get('plan', 'm2v')), kv.get('key', '-')


# ------------------------------------------------------------------ logs ----
class Log:
    """Events of one console log: per-process traces, AIR/Mali misses and TEGU_PSO_REC records."""
    def __init__(self, path):
        self.path = Path(path)
        self.traces, self.air, self.mali, self.records = [], [], [], []
        self.cache_lines = []
        trace, chunks, last = {}, {}, {}
        physical = (part for raw in open(path, errors='replace') for part in GLUED.split(raw) if part)
        for line in physical:
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
                    self.records.append({'proc': proc, 'k': key, 'src': slot['src'], 'text': body, 'n': n,
                                         'chunks': [slot['parts'][j] for j in range(1, n + 1)]})
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


def bundle_profile(macho):
    """The translator profile (tegu_native_profile, a JSON text) compiled into a Nord2Metal Mach-O."""
    data = Path(macho).read_bytes()
    for m in re.finditer(rb'\{\s*"\$schema"', data):
        text = data[m.start():m.start() + 4_000_000].decode('utf-8', 'ignore')
        try:
            obj, end = json.JSONDecoder().raw_decode(text)
        except ValueError:
            continue
        return text[:end].encode()
    raise SystemExit('%s: no translator profile found' % macho)


def load_profile(path):
    data = profile_bytes(Path(path))
    return data, hashlib.sha256(data).hexdigest()


def analyse(log_paths, build, profile_path=DEFAULT_PROFILE, functions_path=DEFAULT_FUNCTIONS, repair=True, repair_budget=20000):
    """Per-log verdicts for every logged AIR miss plus the verified descriptions."""
    functions, tables = Functions(functions_path), fixed_constant_tables()
    profile, profile_hex = load_profile(profile_path)
    report, verified, mali = [], {}, {}
    for path in log_paths:
        log = Log(path)
        row = {'log': str(path), 'air_misses': len(log.air), 'mali_misses': len(log.mali), 'records': len(log.records), 'incomplete_records': log.incomplete_records,
               'verdicts': {}, 'unverified_records': 0}
        verdict = row['verdicts']
        logged = {e['key']: e for e in log.air}
        # 1. full records (device with TEGU_PSO_REC); a damaged one goes to repair_records below
        damaged = {}
        for r in log.records:
            if r['text'].startswith('TPC_MISS v1 mali '):
                # Draw-time Mali variant: verifiable only by the replay (the key covers the SPIR-V); build-independent text.
                text = re.sub(r' key=\w+', ' key=-', r['text'], count=1)
                if re.search(r' vc=\S* fc=\S* opt=\S+$', text):
                    mali[hashlib.sha256(text.encode()).hexdigest()[:16]] = text
                continue
            try:
                p, _ = parse_record(r['text'], 'record')
            except (KeyError, ValueError):
                damaged.setdefault(r['k'], r)
                continue
            if p.profile != profile_hex:
                row['unverified_records'] += 1
                verdict.setdefault(r['k'], 'profile_mismatch')   # --profile is not the bundle's translator profile
                continue
            k = p.key(build, profile).hex()
            if k.startswith(r['k']):
                verdict[r['k']] = 'verified' if r['src'] == 'miss' or r['k'] not in verdict else verdict[r['k']]
                verified[p.description_id()] = p
            else:
                damaged.setdefault(r['k'], r)
        damaged = {k: r for k, r in damaged.items() if k not in {x.key(build, profile).hex()[:16] for x in verified.values()}}
        row['damaged_records'] = len(damaged)
        if damaged and repair:
            fixed, rstats = repair_records(list(damaged.values()), functions, build, profile, budget=repair_budget)
            row['repair'] = dict(rstats)
            for p in fixed.values():
                verified[p.description_id()] = p
                verdict[p.key(build, profile).hex()[:16]] = 'repaired'
        for k in damaged:
            verdict.setdefault(k, 'damaged_record')
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
    return report, verified, mali


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
    s.add_argument('--out', type=Path, default=GPU_BUILD / 'pipeline-corpus/corpus.txt', help='corpus file: one TPC_MISS v1 air record per verified description, merged and deduped across runs (default %(default)s)')
    s.add_argument('--no-repair', action='store_true', help='do not try to recover records cut by the 256-byte console line limit')
    s.add_argument('--repair-budget', type=int, default=20000, help='key evaluations per damaged record')
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
        if a.bundle:
            exe = a.bundle / 'Nord2Metal' if a.bundle.is_dir() else a.bundle
            prof_file = (a.out.parent if a.out else GPU_BUILD / 'pipeline-corpus') / ('profile-%s.json' % build[12:20])
            prof_file.parent.mkdir(parents=True, exist_ok=True)
            prof_file.write_bytes(bundle_profile(exe))
            a.profile = prof_file
        report, verified, mali = analyse(a.logs, build, a.profile, a.functions, not a.no_repair, a.repair_budget)
        corpus = {}
        if a.out and a.out.exists():
            for line in a.out.read_text().splitlines():
                if line.startswith('TPC_MISS v1 mali '):
                    corpus['m' + hashlib.sha256(line.encode()).hexdigest()[:16]] = line
                elif line.startswith('TPC_MISS'):
                    p, _ = parse_record(line)
                    corpus[p.description_id()] = line
        for did, p in verified.items():
            corpus[did] = p.record()
        for did, text in mali.items():
            corpus['m' + hashlib.sha256(text.encode()).hexdigest()[:16]] = text
        if a.out:
            a.out.parent.mkdir(parents=True, exist_ok=True)
            # air records first, then mali (the replay compiles variants of the pipelines it just built)
            keys = sorted(corpus, key=lambda k: (k.startswith('m'), k))
            a.out.write_text(''.join(corpus[k] + '\n' for k in keys))
        for row in report:
            print('%s: air misses %d, %s' % (row['log'], row['air_misses'], json.dumps(row['air_summary'], sort_keys=True)))
        print('verified descriptions: %d, mali variants %d (corpus %d records)' % (len(verified), len(mali), len(corpus)))
        if a.json:
            a.json.write_text(json.dumps({'build': build, 'report': report}, indent=1))
    else:
        for row in mali_coverage(a.logs, a.cache):
            print('%s %s: %d/%d reproduced%s' % (row['log'], row['kind'], row['reproduced'], row['logged'],
                                              '' if not row['missing'] else ' (missing %d)' % len(row['missing'])))



# ----------------------------------------------------- repair of cut lines --
# The device console cuts every line at ~256 bytes, "|process| " prefix included.  The first
# TEGU_PSO_REC emitter (200-character chunks) therefore lost the last 5-7 characters of every
# chunk; later builds emit 120-character chunks (no loss).  A damaged record is not discarded.
# Its true layout is known: every chunk but the last is exactly CHUNK characters, the cut removes
# the tail of each, so each surviving character has an exact position in the record.  Everything
# but the function-constant values, the blend rows and the vertex descriptor follows from the
# function names (library, sorted declared constants, widths); the logged key prefix is the check.
ENTRY = re.compile(r'1:(\d+):0:(fc_\w+):([0-9a-f]+);')
TYPE_BYTES = {49: 1, 53: 1, 51: 3}
_DECLARED = {}


def _declared():
    if not _DECLARED:
        for e in json.load(open(DEFAULT_FUNCTIONS))['entries']:
            if e['name'] in ('fixed_vert_lph_spc', 'fixed_frag_lph_cph'):
                _DECLARED[e['name']] = {c['name']: c['type'] for c in e['constants']}
    return _DECLARED


def _known_text(rec):
    """Record text with None at cut positions, and the chunk size."""
    chunks, n = rec['chunks'], rec['n']
    size = 200 if max(len(c) for c in chunks[:-1] or chunks) >= 150 else 120
    if any(len(c) > size for c in chunks):
        return None   # foreign text glued onto a chunk: no exact positions
    known = []
    for i, c in enumerate(chunks):
        c = c.replace('#', ' ')
        known += list(c)
        if i < n - 1:
            known += [None] * (size - len(c))
    return known


def _matches(known, text, at):
    """text equals the known characters from position at on (cut positions match anything)."""
    for j, ch in enumerate(text):
        k = known[at + j] if at + j < len(known) else ch
        if k is not None and k != ch:
            return False
    return at + len(text) <= len(known) or True


def _enum_rows(known, start, limit=4000):
    """Blend rows ('a/b/.../i,' nine numbers 0..19, mask <= 15) consistent with the known characters from start."""
    out = []

    def go(pos, row):
        if len(out) >= limit:
            return
        field = len(row)
        if field == 9:
            out.append((tuple(row), pos))
            return
        for v in range(0, 16 if field == 8 else 20):
            t = str(v) + (',' if field == 8 else '/')
            ok = True
            for j, ch in enumerate(t):
                k = known[pos + j] if pos + j < len(known) else None
                if k is not None and k != ch:
                    ok = False
                    break
            if ok:
                go(pos + len(t), row + [v])
    go(start, [])
    return out


def repair_records(records, functions, build, profile, budget=20000):
    """records: Log.records with chunks.  Returns ({description_id: Pipeline}, stats)."""
    import itertools
    declared = _declared()
    vs_rows, fs_rows, _ = fixed_constant_tables()
    base = {'fixed_vert_lph_spc': {n: d for n, t, s, d in vs_rows}, 'fixed_frag_lph_cph': {n: d for n, t, s, d in fs_rows}}
    pool9, seen_attr, seen_lay, seen_val = collections.Counter(), collections.Counter(), collections.Counter(), collections.defaultdict(collections.Counter)
    for r in records:
        t = r['text']
        for m in re.finditer(r'(?<![\d/])(\d+(?:/\d+){8})(?=,)', t):
            pool9[tuple(int(x) for x in m.group(1).split('/'))] += 1
        for tag, c in (('attrs', seen_attr), ('layouts', seen_lay)):
            m = re.search(r' %s=(\S*,)(?: |$)' % tag, t)
            if m:
                c[m.group(1)] += 1
        for m in ENTRY.finditer(t):
            seen_val[m.group(2)][m.group(3)] += 1
    for extra in itertools.product((1,), (0, 1), (0,), (1,), (0,), (0,), (1,), (0,), range(16)):
        pool9.setdefault(extra, 0)
    # the UI's standard vertex descriptor (QuartzCore fixed/blur/luma pipelines), always a candidate
    seen_attr.setdefault('0/31/0/1,1/29/16/1,2/29/24/1,3/27/32/1,', 0)
    seen_lay.setdefault('1/48/1/1,', 0)
    pool = [x for x, _ in pool9.most_common()]
    profile_hex = hashlib.sha256(profile).hexdigest()
    stats, out = collections.Counter(), {}
    for rec in records:
        known = _known_text(rec)
        if known is None:
            stats['corrupt_chunk'] += 1
            continue
        joined = ''.join(c for c in known if c is not None)
        head = {k: re.search(r' %s=(\S+)' % k, joined) for k in ('variant', 'format', 'vertex', 'fragment')}
        if not all(head.values()):
            stats['unparsed_head'] += 1
            continue
        variant, fmt = int(head['variant'].group(1)), int(head['format'].group(1))
        vertex, fragment = head['vertex'].group(1), head['fragment'].group(1)
        vlib, flib = functions.library(vertex), functions.library(fragment)
        if not vlib or not flib:
            stats['unknown_function'] += 1
            continue
        # skeleton of everything up to the targets, constants at their defaults
        def skeleton_specs(over):
            sp = []
            for name in (vertex, fragment):
                if name in declared:
                    cs = []
                    for n in sorted(declared[name]):
                        ty = declared[name][n]
                        d = over.get((name, n), base[name].get(n, bytes(TYPE_BYTES[ty])))
                        cs.append(pk.Constant(ty, d if len(d) == TYPE_BYTES[ty] else bytes(TYPE_BYTES[ty]), n))
                    sp.append(pk.specialized_replay(name, cs))
                else:
                    sp.append(pk.specialized_replay(name, []))
            return sp
        vspec0, fspec0 = skeleton_specs({})
        p0 = Pipeline(variant=variant, format=fmt, vertex=vertex, vlib=vlib, vspec=vspec0, fragment=fragment, flib=flib, fspec=fspec0,
                      targets=[], blends=[], attrs=[], layouts=[], profile=profile_hex, msgcap=512)
        key_field = 'key=' + (''.join(c if c else '0' for c in known[20:84]))
        T0 = p0.record('0' * 64)
        cut = T0.index(' targets=')
        # constant values: from visible digits, unknown where the cut hit them
        unknown, over = [], {}
        for m in re.finditer(r'1:(\d+):0:(fc_\w+):([0-9a-f]+);', T0[:cut]):
            ty, n, hx = int(m.group(1)), m.group(2), m.group(3)
            at = m.start(3)
            vals = known[at:at + len(hx)]
            name = fragment if m.start() >= T0.index('fspec=') else vertex
            if None in vals:
                unknown.append((name, n, ty))
            else:
                over[(name, n)] = bytes.fromhex(''.join(vals))
        # consistency of everything that is not a value: the skeleton must match the visible text
        value_pos = set()
        for m in re.finditer(r'1:\d+:0:fc_\w+:([0-9a-f]+);', T0[:cut]):
            value_pos.update(range(m.start(1), m.end(1)))
        bad = [i for i in range(min(cut, len(known))) if known[i] is not None and known[i] != T0[i] and i not in value_pos and not (20 <= i < 84)]
        if bad:
            stats['skeleton_mismatch'] += 1
            continue
        # tail candidates (targets, blends, attrs, layouts), filtered by their known characters
        def tail_text(tg, bl, at, lay):
            def rows(items):
                return ''.join('/'.join(str(v) for v in i) + ',' for i in items)
            return ' targets=%s blends=%s attrs=%s layouts=%s profile=%s msgcap=512' % (rows(tg), rows(bl), at, lay, profile_hex)
        tg_opts = [[(80, 15), (80, 0)], [(80, 0), (80, 15)], [(43, 15), (80, 0)]] if variant in (3, 4) else [[]]
        need = 1 if variant == 2 else (2 if variant == 4 else 0)
        att_opts = [a for a, _ in seen_attr.most_common(4)] or ['']
        lay_opts = [a for a, _ in seen_lay.most_common(4)] or ['']
        cands = []
        for tg in tg_opts:
            for a in att_opts:
                for l in lay_opts:
                    # blend rows: filter each row independently against its known characters
                    pre = tail_text(tg, [], a, l)
                    at_blend = cut + len(' targets=%s blends=' % ''.join('/'.join(map(str, i)) + ',' for i in tg))
                    def row_opts(start):
                        return _enum_rows(known, start)
                    if need == 0:
                        seqs = [[]]
                    elif need == 1:
                        seqs = [[r] for r, _ in row_opts(at_blend)]
                    else:
                        seqs = []
                        for r1, end1 in row_opts(at_blend):
                            seqs += [[r1, r2] for r2, _ in row_opts(end1)]
                    for bl in seqs:
                        text = tail_text(tg, bl, a, l)
                        if _matches(known, text, cut):
                            cands.append((tg, bl, a, l))
        stats['tail_candidates'] += len(cands)
        def tup(x):
            return [tuple(int(v) for v in y.split('/')) for y in x.split(',') if y]
        # unknown constant values: observed values, then 0/1, then (single unknown) every byte
        def domain(name, n, ty):
            width = TYPE_BYTES[ty]
            vals = [bytes.fromhex(h) for h, _ in seen_val[n].most_common() if len(h) == 2 * width]
            for v in range(2 if width == 1 else 1):
                vals.append(bytes([v]) + bytes(width - 1))
            seen = []
            for v in vals:
                if v not in seen:
                    seen.append(v)
            if len(unknown) == 1 and width == 1:
                seen += [bytes([v]) for v in range(256) if bytes([v]) not in seen]
            return seen
        doms = [domain(*u) for u in unknown]
        total = 1
        for d in doms:
            total *= len(d)
        found, tried = None, 0
        key16 = rec['k']
        if total * max(1, len(cands)) > budget * 50:
            doms = [d[:max(2, int((budget * 50 / max(1, len(cands))) ** (1.0 / max(1, len(doms)))))] for d in doms]
        for combo in itertools.product(*doms) if doms else [()]:
            o = dict(over)
            for u, v in zip(unknown, combo):
                o[(u[0], u[1])] = v
            vspec, fspec = skeleton_specs(o)
            for tg, bl, a, l in cands:
                tried += 1
                if tried > budget:
                    break
                p = Pipeline(variant=variant, format=fmt, vertex=vertex, vlib=vlib, vspec=vspec, fragment=fragment, flib=flib, fspec=fspec,
                             targets=tg, blends=bl, attrs=tup(a), layouts=tup(l), profile=profile_hex, msgcap=512, source='repaired')
                if p.key(build, profile).hex().startswith(key16):
                    found = p
                    break
            if found or tried > budget:
                break
        if found:
            stats['repaired'] += 1
            out[found.description_id()] = found
        else:
            stats['unrepaired_budget' if tried > budget else 'unrepaired'] += 1
    return out, stats

if __name__ == '__main__':
    main()
