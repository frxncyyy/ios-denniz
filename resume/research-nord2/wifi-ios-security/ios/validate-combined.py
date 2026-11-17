from pathlib import Path
import subprocess,json,hashlib,sys
P=Path(__file__).resolve().parent;R=P.parents[3];O=P/'combined';rows=[]
plan=[('contracts',['python3','tools/test-nord2-wifi-ios-contract.py','--report',str(O/'contracts.json')]),('client',['python3','tools/test-nord2-wifi-ios-client.py']),('station',['python3','tools/test-nord2-wifi-station.py','--log-dir',str(O/'station')]),('join-owner',['python3','tools/test-nord2-wifi-join-owner.py','--log-dir',str(O/'join-owner')]),('registry',['python3','tools/test-nord2-wifi-ios-registry.py'])]
files=list((R/'shim/nord2-xnu').glob('wifi*'))+list((R/'tools/fixtures').glob('nord2-wifi*/*'))
hashes=lambda:{str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files if f.is_file()};before=hashes()
for name,cmd in plan:
 with (O/(name+'.log')).open('wb') as log:r=subprocess.run(cmd,cwd=R,stdout=log,stderr=subprocess.STDOUT)
 rows.append({'name':name,'argv':cmd,'exit':r.returncode});(O/'report.json').write_text(json.dumps({'tests':rows,'source':before,'unchanged':before==hashes()},indent=2)+'\n');print(name,r.returncode,flush=True)
 if r.returncode: print((O/(name+'.log')).read_text()[-5000:]);sys.exit(r.returncode)
print('All combined canonical tests PASS',flush=True)
