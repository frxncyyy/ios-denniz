#!/usr/bin/env python3
"""Per-scenario full-screen job table + device-model prediction (host only).

Inputs: fsjobs-<tag>-pf0.json from fsjobs.py (job census of the host replay) and
the device-fit frame model (gpu/tools/frame_model.py, c6jb logs).  Encode ms per
scenario is taken from the newest frame_bench history record ('+tree' column,
measured host change), so only the job structure and the preload area change.
Usage: model_table.py BEFORE_TAG AFTER_TAG
"""
import json, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
GPU = HERE.parents[2]
sys.path.insert(0, str(GPU / 'tools'))
import frame_model as fm  # noqa: E402

SCN_DRAWS = {'tick': 5, 'small': 18, 'medium': 45, 'large': 104, 'ca200': 200, 'blur_heavy': 58}
W = fm.SCEN_DEFAULTS['wait_floor_ms']
LEVERS = [('tree', dict()),
          ('P3+654+async', dict(wait_ms=W, clock_mhz=654, async_wait=True))]


def load(tag):
    return json.loads((HERE / ('fsjobs-%s-pf0.json' % tag)).read_text())


def enc_ms():
    rec = json.loads((GPU / 'build/frame-bench-20261026/history.jsonl').read_text().splitlines()[-1])
    return {k.split('/')[0]: v['enc_ms'] for k, v in rec['whatif'].items() if k.endswith('/+tree')}


def cover(d):
    """Tile-weighted coverage of the full-screen jobs (scissor-union upper bound of INTERSECT tiles)."""
    c = d['full_cover']
    return sum(c) / len(c) if c else 1.0


def main():
    before, after = load(sys.argv[1]), load(sys.argv[2])
    model, _ = fm.build_model()
    enc = enc_ms()
    rows = []
    hdr = '%-10s %9s %9s %6s %6s' % ('scenario', 'fs before', 'fs after', 'other', 'cover')
    for name, _ in LEVERS:
        hdr += ' | %-12s %7s %7s %7s' % (name, 'before', 'after', '+isect')
    print(hdr)
    for scn in SCN_DRAWS:
        b, a = before[scn], after[scn]
        line = '%-10s %9d %9d %6d %6.3f' % (scn, b['full'], a['full'], a['other'], cover(a))
        rec = dict(scenario=scn, fs_before=b['full'], fs_after=a['full'], other=a['other'], cover=round(cover(a), 3))
        for name, ov in LEVERS:
            res = []
            for shape_src, area in ((b, 1.0), (a, 1.0), (a, cover(a))):
                shape = {'fs_load': shape_src['full'], 'off_clear': shape_src['other']}
                r = fm.scenario_ms(model, shape, SCN_DRAWS[scn], preload_area=area, **ov)
                gpu = r['gpu_ms']
                res.append(max(gpu, enc.get(scn, r['enc_ms'])))   # pipelined: max(GPU, encode)
            line += ' | %-12s %7.1f %7.1f %7.1f' % ('', res[0], res[1], res[2])
            rec[name] = [round(x, 2) for x in res]
        rows.append(rec)
        print(line)
    print('cells: predicted device ms/frame, pipelined max(GPU, encode); encode = frame_bench +tree measured; '
          'GPU = c6jb fit (fs 5.75 / other 3.39 ms per job, wait 2.5 ms assumed) with frame_model what-if; '
          '+isect = preload_area = scissor-union tile coverage (upper bound), preload_share %.1f ASSUMED'
          % fm.SCEN_DEFAULTS['preload_share'])
    (HERE / ('model-%s-vs-%s.json' % (sys.argv[1], sys.argv[2]))).write_text(json.dumps(rows, indent=1))


if __name__ == '__main__':
    main()
