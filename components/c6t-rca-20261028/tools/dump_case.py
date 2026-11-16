import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'air-nir-20261028/tools'))
import air_vs_m2v as A
want = set(sys.argv[1:])
for cid, c in A.cases_all():
    if cid in want:
        cap = c['cap']
        print(cid, c['name'], 'targets', cap['targets'], 'blends', cap['blends'], 'attrs', cap['attrs'], 'layouts', cap['layouts'])
        nz = [(r.get('name'), r.get('value_hex')) for r in cap['fc'] if r.get('value_hex') and int(r['value_hex'], 16)]
        print('  fc nonzero:', nz)
        print('  vc nonzero:', [(r.get('name'), r.get('value_hex')) for r in cap['vc'] if r.get('value_hex') and int(r['value_hex'], 16)])
