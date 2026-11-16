#!/usr/bin/env python3
"""Per-function coverage ledger from two replay runs (before/after).

ledger.py <before run dir> <after run dir> <out dir>
Writes <out>/ledger.jsonl (one row per unique kernel/vertex/fragment AIR function + every
captured device pipeline) and <out>/LEDGER.md (counts per class, causes, exclusions).
A vertex/fragment function counts ok when at least one replayed pair containing it
compiled; its provenance is the best pair's (captured > inferred+synthetic)."""
import collections, json, re, sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from summarize import cause, CLASSES  # noqa: E402
from replay import process_class, CORPUS  # noqa: E402

ORDER = {'ok': 0, 'refused': 1, 'timeout': 2, 'crash': 3}

KINDS = [  # (category, substrings) - first match wins
    ('ok', ('ok',)),
    ('compile_budget_exceeded (>30 s CPU / 45 s wall; not a refusal)', ('timeout',)),
    ('diagnostic_input_mismatch (synthetic descriptor/constants/pairing, not shader semantics)',
     ('invalid descriptor: ColorAttachment', 'MRT fragment writes no bound', 'Selected colour output is not written',
      'UnmatchedVarying', 'needs pipeline input: FunctionConstant', 'MRT colour output beyond')),
    ('runtime_input_not_expressible_in_bridge_API (MTLLinkedFunctions / function tables / tile render pass)',
     ('LinkedFunction', 'VisibleFunctionTable', 'TileAttachments', 'StageInputDescriptor')),
    ('tessellation (post-tessellation vertex stage, no Mali tessellator; bridge expanded-stage refusal)',
     ('expanded render stage', 'invalid descriptor: VertexAttribute')),
    ('renderer/ABI feature not implemented (formats, depth, viewport arrays, layers, FS storage images, argument buffers, bindings>15)',
     ('bounded class', 'mrt_format_ok', 'FragDepth', 'ViewportIndex', 'Layer', 'storage images', 'storage buffers must be read-only',
      'binding index above', 'physical pointers', 'descriptor arrays', 'interlock', 'Fragment builtin inputs', 'stage limit',
      'SampleMask', 'ClipDistance', 'CullDistance')),
    ('translator semantics not implemented (AIR->SPIR-V)', ('Imageblocks', 'Lowering(Access)', 'StaticInitializer', 'ArgumentBufferObjects', 'CallTypeMismatch', 'Canon(')),
    ('installed AIR uses a legacy intrinsic signature (decoder compatibility)', ('SignatureMismatch',)),
    ('empty program after specialization (backend emits no code)', ('emitted no code',)),
]


def category(c):
    if c == 'ok':
        return 'ok'
    for name, keys in KINDS[1:]:
        if any(k in c for k in keys):
            return name
    return 'other: ' + c[:60]


def load(run):
    rows = {}
    for line in open(Path(run) / 'results.jsonl'):
        r = json.loads(line)
        rows[r['id']] = r
    return rows


def func_view(rows):
    """(stage, air_sha) -> best row."""
    out = {}

    def put(key, r, role):
        cur = out.get(key)
        rank = (ORDER.get(r['outcome'], 9), 0 if 'captured' in r.get('provenance', '') else 1)
        if cur is None or rank < cur[0]:
            out[key] = (rank, r, role)
    for r in rows.values():
        if r['kind'] == 'compute':
            put(('kernel', r['air_sha256']), r, 'kernel')
        elif r['kind'] == 'graphics':
            put(('vertex', r['vertex_air_sha256']), r, 'vertex')
            put(('fragment', r['fragment_air_sha256']), r, 'fragment')
    return out


def provenance(r):
    p = r.get('provenance', '')
    orig = r.get('constants_origin') or []
    if 'captured_device' in p:
        return 'captured_device'
    if p.startswith('captured'):
        return 'captured_donor'
    base = 'inferred+synthetic_descriptor' if r['kind'] == 'graphics' else 'installed_air'
    for key, tag in (('linked_origin', '+synthetic_linked_functions'), ('tile_origin', '+synthetic_tile_descriptor'), ('stage_input_origin', '+synthetic_stage_input'),
                     ('tessellation', '+tessellation_eval_as_vs')):
        if r.get(key):
            base += tag
    if any(o.startswith('captured_donor') for o in orig):
        base += '+captured_donor_constants'
    if any(o.startswith('synthetic') for o in orig):
        base += '+synthetic_constants'
    return base


def main(before, after, out):
    out = Path(out)
    B, A = load(before), load(after)
    fb, fa = func_view(B), func_view(A)
    funcs = json.load(open(CORPUS / 'functions.json'))['entries']
    meta = {}
    for e in funcs:
        meta.setdefault((e['stage'], e['air_sha256']), e)
    ledger = []
    for key in sorted(set(fb) | set(fa)):
        e = meta.get(key, {})
        rb, ra = fb.get(key), fa.get(key)
        r = ra[1] if ra else rb[1]
        ledger.append({'stage': key[0], 'air_sha256': key[1], 'name': e.get('name') or r['name'], 'library_source': e.get('library_source', r['library_source']),
                       'class': process_class(e.get('library_source', r['library_source'])),
                       'before': rb[1]['outcome'] if rb else None, 'after': r['outcome'], 'after_cause': cause(r),
                       'after_provenance': provenance(r), 'via_case': r['id'], 'constants_declared': len(e.get('constants') or []),
                       'after_category': category(cause(r))})
    cap = [{'stage': 'pipeline', 'air_sha256': None, 'name': r['name'], 'library_source': r['library_source'], 'class': r['class'],
            'before': B.get(r['id'], {}).get('outcome'), 'after': r['outcome'], 'after_cause': cause(r), 'after_provenance': 'captured_device',
            'via_case': r['id']} for r in A.values() if r['kind'] == 'captured']
    ext = {}
    tl = Path(out) / 'runs/timeouts-long/results.jsonl'
    if tl.exists():
        for line in open(tl):
            r = json.loads(line)
            ext[r['id']] = r
    for row in ledger:
        r = ext.get(row['via_case'])
        if row['after'] == 'timeout' and r:
            row['after_extended_budget'] = {'outcome': r['outcome'], 'seconds': r['seconds'], 'cpu_limit_s': 240}
    with open(out / 'ledger.jsonl', 'w') as f:
        for row in ledger + cap:
            f.write(json.dumps(row) + '\n')
    # tables
    L = []
    L.append('# Installed iOS 16.5 shader coverage ledger (host replay, shader-coverage-opus)\n')
    ident_b = json.load(open(Path(before) / 'identity.json'))
    ident_a = json.load(open(Path(after) / 'identity.json'))
    L.append('Host replay only: AIR -> SPIR-V (libtegu_air_bridge) -> NIR -> Mali arch9 (libnord2_mali_runtime). '
             'No GPU execution, no device. "ok" = translated AND Mali code generated through the production entry '
             '(compute: tegu_mali_compile then compute_v2; graphics: tegu_mali_compile_generic_pair_options with '
             'DRAW_PARAMETERS|EXTENDED_VARYINGS|THREAD_STORAGE, true-MRT/SELECT_RT as render_pipeline.inc).\n')
    L.append('| run | bridge | Mali runtime | profile |\n|---|---|---|---|')
    for n, i in (('before', ident_b), ('after', ident_a)):
        L.append(f"| {n} | {i['bridge']['sha256'][:12]} | {i['compiler']['sha256'][:12]} | {i['profile']['sha256'][:12]} |")
    L.append('')
    L.append('## Functions ok per process class (unique AIR functions; vertex/fragment ok if any replayed pair with it compiled)\n')
    L.append('| class | stage | functions | ok before | ok after | refused after | timeout after | crash after |\n|---|---|---|---|---|---|---|---|')
    tot = collections.Counter()
    for cl in CLASSES:
        for st in ('kernel', 'vertex', 'fragment'):
            rs = [x for x in ledger if x['class'] == cl and x['stage'] == st]
            if not rs:
                continue
            c = collections.Counter(x['after'] for x in rs)
            ob = sum(1 for x in rs if x['before'] == 'ok')
            L.append(f"| {cl} | {st} | {len(rs)} | {ob} | {c['ok']} | {c['refused']} | {c['timeout']} | {c['crash']} |")
            tot['n'] += len(rs); tot['ob'] += ob; tot['oa'] += c['ok']
    L.append(f"| **all** | | {tot['n']} | {tot['ob']} | {tot['oa']} | | | |\n")
    L.append('## Captured device pipelines (gpu/build/pipeline-corpus/corpus.txt, CoreAnimation/backboardd TPC_MISS records)\n')
    cb = collections.Counter(B[r['via_case']]['outcome'] for r in cap if r['via_case'] in B)
    ca = collections.Counter(r['after'] for r in cap)
    L.append(f"{len(cap)} records: before {dict(cb)}, after {dict(ca)} (exact captured constants, targets, blends, vertex layouts).\n")
    L.append('## Provenance of the "ok after" functions\n')
    pc = collections.Counter(x['after_provenance'] for x in ledger if x['after'] == 'ok')
    for k, v in pc.most_common():
        L.append(f'- {k}: {v}')
    L.append('\nsynthetic_constants = corpus_inputs audit plan values (origin synthetic_diagnostic:canonical, bool/int 1, float 0.5) '
             'or the same rule for constants the plan did not list (origin ...(completion)); they prove one reachable '
             'specialization translates/compiles, never application behaviour. inferred+synthetic_descriptor = same-library '
             'ABI pairing and synthetic vertex/attachment descriptors (attachment formats chosen by declared target type).\n')
    tos = [x for x in ledger if x['after'] == 'timeout']
    te = collections.Counter(x.get('after_extended_budget', {}).get('outcome', 'not rerun') for x in tos)
    L.append(f'## Compile-budget reruns\n\n{len(tos)} functions exceeded the 30 s CPU / 45 s wall replay budget (all in the Mali phase: '
             f'Bifrost register allocation). Rerun with 240 s CPU / 300 s wall: {dict(te)}. They count as "timeout" in the tables, '
             'not as ok and not as refusals.\n')
    L.append('## Remaining failures: unsupported semantics vs wrong diagnostic inputs (after)\n')
    L.append('| category | functions | coreanimation | ui_frameworks | other classes |\n|---|---|---|---|---|')
    cg = collections.defaultdict(list)
    for x in ledger:
        if x['after'] != 'ok':
            cg[x['after_category']].append(x)
    for k, rs in sorted(cg.items(), key=lambda kv: -len(kv[1])):
        ca = sum(1 for r in rs if r['class'] == 'coreanimation'); ui = sum(1 for r in rs if r['class'] == 'ui_frameworks')
        L.append(f'| {k} | {len(rs)} | {ca} | {ui} | {len(rs) - ca - ui} |')
    L.append('')
    L.append('## Remaining failures by root cause (after), ranked CoreAnimation, UI, then count\n')
    g = collections.defaultdict(list)
    for x in ledger:
        if x['after'] != 'ok':
            g[x['after_cause']].append(x)
    ranked = sorted(g.items(), key=lambda kv: (-sum(1 for r in kv[1] if r['class'] == 'coreanimation'),
                                               -sum(1 for r in kv[1] if r['class'] == 'ui_frameworks'), -len(kv[1])))
    L.append('| functions | by class | cause | example |\n|---|---|---|---|')
    for c, rs in ranked:
        bc = dict(collections.Counter(r['class'] for r in rs))
        L.append(f"| {len(rs)} | {bc} | {c.replace('|', '/')} | {rs[0]['name'][:50]} |")
    L.append('')
    L.append('## Every remaining CoreAnimation / UI-framework function (after)\n')
    L.append('| class | stage | function | library | cause | provenance |\n|---|---|---|---|---|---|')
    for x in sorted((x for x in ledger if x['after'] != 'ok' and x['class'] in ('coreanimation', 'ui_frameworks')),
                    key=lambda x: (x['class'] != 'coreanimation', x['library_source'], x['name'])):
        if x['class'] == 'ui_frameworks' and 'AppleMetalGLRenderer' in x['library_source'] and 'bounded class' in x['after_cause']:
            continue
        L.append(f"| {x['class']} | {x['stage']} | {x['name'][:48]} | {x['library_source'].split('/')[-2]} | {x['after_cause'][:110].replace('|', '/')} | {x['after_provenance']} |")
    gl = sum(1 for x in ledger if x['after'] != 'ok' and 'AppleMetalGLRenderer' in x['library_source'] and 'bounded class' in x['after_cause'])
    L.append(f"| ui_frameworks | fragment | ({gl} AppleMetalGLRenderer integer-target clear/blit functions) | AppleMetalGLRenderer.bundle | translate: attachment format or write mask outside bounded class | inferred+synthetic_descriptor |\n")
    L.append('## Not replayed (outside the pipeline ABI) - kept, not counted as covered\n')
    st = collections.Counter(e['stage'] for e in funcs)
    us = collections.defaultdict(set)
    for e in funcs:
        us[e['stage']].add(e['air_sha256'])
    for k in ('visible', 'other-5', 'exported', 'intersection'):
        L.append(f'- {k}: {st[k]} entries / {len(us[k])} unique AIR (visible/intersection functions are linked into a pipeline only through function tables; no standalone pipeline ABI)')
    summ = json.load(open(CORPUS / 'summary.log')) if (CORPUS / 'summary.log').exists() else {}
    inv = summ.get('inventory', {})
    L.append(f"- AGX runtime helpers: {inv.get('AGX_runtime_helpers')}; native Apple GPU archive symbols: {inv.get('native_Apple_GPU_archive_symbols')} (no AIR)")
    L.append(f"- graphics entries with no same-library pair: 9 (graphics-jobs.json unpaired)")
    L.append(f"- vertex/fragment AIR functions never placed in a replayed pair: "
             f"{len(us['vertex']) - sum(1 for x in ledger if x['stage']=='vertex')} vertex, {len(us['fragment']) - sum(1 for x in ledger if x['stage']=='fragment')} fragment\n")
    extra = Path(out) / 'LEDGER-notes.md'
    if extra.exists():
        L.append(extra.read_text())
    (out / 'LEDGER.md').write_text('\n'.join(L) + '\n')
    print('\n'.join(L[:60]))


if __name__ == '__main__':
    main(*sys.argv[1:4])
