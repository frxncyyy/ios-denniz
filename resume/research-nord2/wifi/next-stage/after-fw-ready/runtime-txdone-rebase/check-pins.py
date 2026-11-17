#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,subprocess
p=Path(__file__).resolve().parent
root=p.parents[5]
manifest=json.loads((p/'manifest.json').read_text())
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()==manifest['canonical_commit']
def sha(b): return hashlib.sha256(b).hexdigest()
for item in manifest['snapshots']:
    current=(root/item['canonical']).read_bytes()
    snapshot=(p/item['snapshot']).read_bytes()
    assert sha(current)==item['canonical_sha256'],item['canonical']
    assert sha(snapshot)==item['snapshot_sha256'],item['snapshot']
    if item['canonical'].endswith('/wifi_main_bind.inc'):
        assert snapshot.replace(b'#include "baseline/failure_drain.inc"',b'#include "failure_drain.inc"')==current
    else:
        assert snapshot==current,item['snapshot']
for name,stamp in manifest['private_sources'].items():
    assert sha((p/name).read_bytes())==stamp,name
print('PASS pinned TXDONE rebase: canonical commit/snapshots and private source hashes')
