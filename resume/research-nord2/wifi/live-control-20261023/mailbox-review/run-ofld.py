from pathlib import Path
import importlib.util,tempfile,shutil,json,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file());A=R/'research/nord2/wifi/claude-opus-dhcp-ecb0/resume-20261023/tests/run.py'
spec=importlib.util.spec_from_file_location('build_helpers',A);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
with tempfile.TemporaryDirectory(prefix='nwl-ofld-') as td:
 d=Path(td);tree=d/'shim';shutil.copytree(m.S,tree);(d/'test-ofld-rx.c').write_bytes((P/'proposed/tools/fixtures/nord2-wifi-live-control/ofld-profile.c').read_bytes());m.HERE=d;r=m.build_run(tree,'ofld',P,[]);out={k:r.get(k) for k in ['compiled','exit','last','sanitizer']};out['passed']=r.get('compiled') and r.get('exit')==0 and not r.get('sanitizer');out['runtime_sha256']=hashlib.sha256((tree/'wifi_station_transport.c').read_bytes()).hexdigest();(P/'ofld-profile-result.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out));raise SystemExit(0 if out['passed'] else 1)
