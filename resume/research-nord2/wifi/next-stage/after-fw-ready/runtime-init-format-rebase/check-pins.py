#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
p=Path(__file__).resolve().parent;root=p.parents[5]
m=json.loads((p/'manifest.json').read_text())
for e in m['snapshots']:
 raw=(root/e['canonical']).read_bytes();snap=(p/e['snapshot']).read_bytes()
 assert hashlib.sha256(raw).hexdigest()==e['canonical_sha256']
 assert hashlib.sha256(snap).hexdigest()==e['snapshot_sha256']
 if e['canonical'].endswith('/wifi_main_bind.inc'):snap=snap.replace(b'#include "baseline/failure_drain.inc"',b'#include "failure_drain.inc"')
 assert snap==raw
for n,h in m['private_sources'].items():assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h
print('PASS exact current canonical snapshot and private source pins')
