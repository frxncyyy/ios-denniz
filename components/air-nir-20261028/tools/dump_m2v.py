#!/usr/bin/env python3
"""Dump the m2v (metal2vulkan) SPIR-V of captured CA pipelines (corpus.txt p-NNN) for interface study."""
import sys, ctypes as C
from pathlib import Path
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'shader-coverage-20261027/tools'))
import replay as R
out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
ids = sys.argv[2:]
L = R.Libs(R.GPU / 'build/deepseek-parallel-20261025/workers/compute/cargo-target/release/libtegu_air_bridge.so',
           R.GPU / 'build/mesa-build/src/panfrost/clc/libnord2_mali_runtime.so')
profile = R.live.profile_bytes(R.GPU / 'build/tegu-frontend/tegu-air-profile.h')
def cases_all():
    import json
    libs = {e['library_sha256']: e for e in json.load(open(R.CORPUS / 'functions.json'))['entries']}
    seen = {}; n = 0
    for line in open(R.GPU / 'build/pipeline-corpus/corpus.txt'):
        if not line.startswith('TPC_MISS v1 air '): continue
        f = dict(x.split('=', 1) for x in line.split() if '=' in x)
        k = tuple(sorted((a, b) for a, b in f.items() if a not in ('key', 'profile', 'msgcap')))
        if k in seen: continue
        vs = R.parse_spec(f['vspec'])[1] if f['vspec'] != '-' else []
        fs = R.parse_spec(f['fspec'])[1] if f['fspec'] != '-' else []
        sp = lambda key: [tuple(map(int, t.split('/'))) for t in f.get(key, '').rstrip(',').split(',') if t]
        cid = 'a-%03d' % n; n += 1; seen[k] = cid
        yield cid, {'name': f['vertex'] + ' + ' + f['fragment'], 'cap': {'library': libs[f['vlib']]['library_saved_as'], 'vertex': f['vertex'], 'fragment': f['fragment'],
               'vc': vs, 'fc': fs, 'targets': sp('targets'), 'blends': sp('blends'), 'attrs': sp('attrs'), 'layouts': sp('layouts'), 'format': int(f.get('format', 0))}}
cases = dict(cases_all())
if ids == ['list']:
    for k, v in cases.items(): print(k, v['name'])
    sys.exit(0)
for i in ids:
    c = cases[i]['cap']; err = C.create_string_buffer(4096)
    base = R.open_lib(L, R.CORPUS / c['library'], err)
    vh = R.specialize(L, base, c['vertex'], c['vc'], err); fh = R.specialize(L, base, c['fragment'], c['fc'], err)
    n = max(1, len(c['targets']))
    at = (R.Attachment * n)(*[R.Attachment(t[0], t[1]) for t in c['targets']])
    bl = (R.live.Blend * n)(*[R.live.Blend(*b) for b in c['blends']])
    attrs = (R.live.VertexAttribute * max(1, len(c['attrs'])))(*[R.live.VertexAttribute(*a) for a in c['attrs']])
    lays = (R.live.VertexLayout * max(1, len(c['layouts'])))(*[R.live.VertexLayout(*a) for a in c['layouts']])
    blobs = [R.live.Blob(), R.live.Blob(), R.live.Blob()]
    vn, fn = c['vertex'].encode(), c['fragment'].encode()
    if c['targets']:
        p = L.b.tegu_air_render_pipeline_create_with_attachment_blends(vh, vn, len(vn), fh, fn, len(fn), at, len(c['targets']), bl,
            attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    else:
        p = L.b.tegu_air_render_pipeline_create_with_vertex_descriptor(vh, vn, len(vn), fh, fn, len(fn), c['format'],
            attrs, len(c['attrs']), lays, len(c['layouts']), profile, len(profile), *[C.byref(x) for x in blobs], err, len(err))
    if not p:
        print(i, 'refused', err.value); continue
    for b, s in zip(blobs, ('vs.spv', 'fs.spv', 'meta.bin')):
        (out / f'{i}.{s}').write_bytes(C.string_at(b.bytes, b.length))
    print(i, c['vertex'], c['fragment'], [b.length for b in blobs])
