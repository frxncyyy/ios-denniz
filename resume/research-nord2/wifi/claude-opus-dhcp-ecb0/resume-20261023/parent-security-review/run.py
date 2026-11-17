from pathlib import Path
import importlib.util,tempfile,json
P=Path(__file__).resolve().parent; C=P.parent
sp=importlib.util.spec_from_file_location('authored_runner',C/'tests/run.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
es=importlib.util.spec_from_file_location('authored_edits',C/'candidate/edits.py');e=importlib.util.module_from_spec(es);es.loader.exec_module(e)
m.HERE=P
with tempfile.TemporaryDirectory(prefix='ofld-independent-') as td:
 tree=Path(td)/'candidate';e.apply(m.S,tree)
 r=m.build_run(tree,'security',P,['-DCANDIDATE_BUILD'])
 result={k:r.get(k) for k in ['compiled','exit','last','sanitizer']};result['passed']=r.get('compiled') and r.get('exit')==0 and not r.get('sanitizer') and r.get('last','').startswith('PASS independent OFLD')
 (P/'test-result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
