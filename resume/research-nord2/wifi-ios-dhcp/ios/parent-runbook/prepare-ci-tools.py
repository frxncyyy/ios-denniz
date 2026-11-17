#!/usr/bin/env python3
"""Parent convenience: create fresh local watcher/audit templates only.
No GH command, CI dispatch/download, build, canonical/gate/device action.
"""
from pathlib import Path
import argparse,hashlib,json,re
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file())
a=argparse.ArgumentParser();a.add_argument('--run',type=int,required=True);a.add_argument('--source',required=True);v=a.parse_args()
assert v.run>0 and re.fullmatch('[0-9a-f]{40}',v.source)
assert v.run!=39859151068 and v.source!='b441429316787f81d62106e7a9892f282b28e174'
old=R/'research/ci-39859151068';out=R/f'research/ci-{v.run}';assert not out.exists(),'Preserve existing CI evidence';out.mkdir()
records={}
for name in ('watch.py','audit-artifact.py'):
 p=old/name;s=p.read_text();assert s.count('b441429316787f81d62106e7a9892f282b28e174')==1
 s=s.replace('b441429316787f81d62106e7a9892f282b28e174',v.source)
 if name=='watch.py':assert s.count("run='39859151068'")==1;s=s.replace("run='39859151068'",f"run='{v.run}'")
 (out/name).write_text(s);records[name]={'origin':str(p.relative_to(R)),'origin_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest()}
(out/'prepared-tools.json').write_text(json.dumps({'state':'TEMPLATES_ONLY_NOT_EXECUTED','ci_run':v.run,'source_commit':v.source,'attempt_required':1,'members':records,'GH_build_device_or_gate_actions':False},indent=2)+'\n');print('Prepared local templates only:',out.relative_to(R))
