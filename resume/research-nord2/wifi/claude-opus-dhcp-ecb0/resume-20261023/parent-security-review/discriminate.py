from pathlib import Path
import importlib.util,tempfile,json,shutil
P=Path(__file__).resolve().parent; C=P.parent
sp=importlib.util.spec_from_file_location('authored_runner',C/'tests/run.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
es=importlib.util.spec_from_file_location('authored_edits',C/'candidate/edits.py');e=importlib.util.module_from_spec(es);es.loader.exec_module(e)
F=P/'discrimination';F.mkdir(exist_ok=True);(F/'test-ofld-rx.c').write_bytes((P/'authored-test-fixed.c').read_bytes());m.HERE=F
with tempfile.TemporaryDirectory(prefix='ofld-discriminate-') as td:
 td=Path(td);patched=td/'candidate';base=td/'baseline';e.apply(m.S,patched);shutil.copytree(m.S,base)
 b=m.build_run(base,'baseline',F,[]);c=m.build_run(patched,'candidate',F,['-DCANDIDATE_BUILD'])
 def good(r,marker):return r.get('compiled') and r.get('exit')==0 and not r.get('sanitizer') and marker in r.get('lines',[]) and r.get('last','').startswith('PASS ofld-rx')
 result={'passed':bool(good(b,m.BASELINE_MARK) and good(c,m.CANDIDATE_MARK)),'baseline':{k:b.get(k) for k in ['compiled','exit','last','sanitizer']},'candidate':{k:c.get(k) for k in ['compiled','exit','last','sanitizer']},'synthetic_only':True,'dhcp_proven':False}
 (F/'report.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result));raise SystemExit(0 if result['passed'] else 1)
