from pathlib import Path
import subprocess,json,time,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').is_file())
O=P/'local-tests';O.mkdir(exist_ok=True)
commands=[
 ('normal-tx',['tools/test-nord2-wifi-normal-tx.py','--log-dir',str(O/'normal-tx')]),
 ('gtk-rekey',['tools/test-nord2-wifi-gtk-rekey.py','--log-dir',str(O/'gtk-rekey')]),
 ('station',['tools/test-nord2-wifi-station.py','--log-dir',str(O/'station')]),
 ('data-diag',['tools/test-nord2-wifi-data-diag.py','--log-dir',str(O/'data-diag')]),
 ('join-owner',['tools/test-nord2-wifi-join-owner.py','--log-dir',str(O/'join-owner')]),
 ('scan-return',['tools/test-nord2-wifi-scan-return.py','--log-dir',str(O/'scan-return')]),
 ('scan-service',['tools/test-nord2-wifi-scan-service.py','--log-dir',str(O/'scan-service')]),
 ('normal-backend',['tools/test-nord2-wifi-normal.py','--only','full','--report',str(O/'normal-backend.json')]),
 ('ios-contract',['tools/test-nord2-wifi-ios-contract.py','--report',str(O/'ios-contract.json')]),
 ('ios-client',['tools/test-nord2-wifi-ios-client.py']),
 ('connection-reader',['tools/test-nord2-wifi-connection-log.py','--report',str(O/'connection-reader.json')]),
 ('wpa-diag',['tools/test-nord2-wifi-wpa-diag.py','--log-dir',str(O/'wpa-diag')]),
 ('data-reader',['tools/test-nord2-wifi-data-diag-reader.py'])]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
pins={str(p.relative_to(R)):sha(p) for p in (R/'shim/nord2-xnu').iterdir() if p.is_file() and p.suffix in ('.c','.cpp','.h','.inc')}
prior=O/'combined.json'
results=json.loads(prior.read_text())['results'] if prior.exists() else []
passed={x['name'] for x in results if x['exit']==0}
for name,args in commands:
 if name in passed:continue
 start=time.monotonic()
 if (O/(name+'.log')).exists():
  (O/(name+'.log')).rename(O/(name+'.failed-attempt'+str(sum(x['name']==name for x in results))+'.log'))
 with (O/(name+'.log')).open('wb') as log:
  r=subprocess.run(['python3',*args],cwd=R,stdout=log,stderr=subprocess.STDOUT)
 results.append(dict(name=name,args=args,exit=r.returncode,seconds=round(time.monotonic()-start,1),log_sha256=sha(O/(name+'.log'))))
 (O/'combined.json').write_text(json.dumps(dict(results=results,source_pins=pins),indent=2)+'\n')
 print(name,r.returncode,round(time.monotonic()-start,1),flush=True)
 if r.returncode:raise SystemExit(r.returncode)
assert all(sha(R/name)==h for name,h in pins.items()),'Runtime sources changed during tests'
print('ALL_COMBINED_TESTS_PASS',flush=True)
