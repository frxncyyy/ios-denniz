from pathlib import Path
import tempfile,shutil,subprocess,hashlib,json
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'AGENTS.md').exists());B=P/'_build';B.mkdir(exist_ok=True);records=[]
with tempfile.TemporaryDirectory(prefix='niwj-canonical-') as t:
 d=Path(t);shutil.copytree(R/'shim/nord2-xnu',d/'shim/nord2-xnu');shutil.copytree(R/'tools/fixtures/nord2-wifi-ios-client',d/'tools/fixtures/nord2-wifi-ios-client');shutil.copytree(R/'tools/fixtures/nord2-wifi-ios-contract',d/'tools/fixtures/nord2-wifi-ios-contract')
 shutil.copytree(P/'proposed',d,dirs_exist_ok=True)
 for name in ('test-nord2-wifi-ios-client.py','test-nord2-wifi-ios-contract.py'):
  cmd=['python3',str(d/'tools'/name)];z=subprocess.run(cmd,capture_output=True,text=True);(B/(name+'.log')).write_text(z.stdout+z.stderr);print(name,z.returncode);print(z.stdout[-500:]);print(z.stderr[-1500:]);z.check_returncode();records.append({'runner':name,'result':'PASS','source_mode':'default canonical paths in isolated temp root, canonical base + exact private v3 overlay; no private runtime dependencies','log_sha256':hashlib.sha256((B/(name+'.log')).read_bytes()).hexdigest()})
(P/'promotion-verification.json').write_text(json.dumps(records,indent=2)+'\n')
