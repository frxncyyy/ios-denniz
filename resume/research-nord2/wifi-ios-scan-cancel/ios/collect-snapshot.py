from pathlib import Path
import subprocess,sys,hashlib,json,re
D=Path(__file__).resolve().parent;R=next(p for p in D.parents if (p/'AGENTS.md').is_file());P=R/'research/nord2/wifi-ios-live/ios';name=sys.argv[1];assert re.fullmatch(r'[a-z0-9-]+',name)
dependency=json.loads((P/'reader-dependencies.json').read_text())
for dep_path in ('tools/nord2-wifi-rx-error.py','tools/nord2-wifi-tx-correlation.py'):
 item=next(x for x in dependency['pins'] if x['path']==dep_path and x.get('canonical_read_only_dependency'))
 assert hashlib.sha256((R/dep_path).read_bytes()).hexdigest()==item['sha256'],'supplemental reader dependency changed'
rx_reader=R/'tools/nord2-wifi-rx-error.py';tx_reader=R/'tools/nord2-wifi-tx-correlation.py'
readers=D/'readers'
manifest=readers/'manifest.json'
assert hashlib.sha256(manifest.read_bytes()).hexdigest()=='9cebaf0c6a81863ad631a39c7a2f3a885c80e79343b8599f97ed252ca72d183b'
for dep_name,h in json.loads(manifest.read_text())['files'].items():
 assert hashlib.sha256((readers/dep_name).read_bytes()).hexdigest()==h,dep_name
Q=D/'hardware-review';Q.mkdir(exist_ok=True);f=Q/(name+'.log');b=(Path(sys.argv[2]) if len(sys.argv)>2 else D/'hardware-live/xnu-live.log').read_bytes();f.open('xb').write(b)
commands={'station-fault':[readers/'nord2-wifi-station-fault.py',f],'backend':[readers/'parse-backend.py',f],'ios':[readers/'parse-ios-service.py',f],'connection':[readers/'parse-connection.py',f],'wpa':[R/'tools/nord2-wifi-wpa-diag.py',f],'data':[R/'tools/nord2-wifi-data-diag.py',f,'--allow-prefix'],'extended':[P/'extended-reader/proposed/tools/nord2-wifi-data-extended.py',f],'rx-error':[rx_reader,f],'tx-correlation':[tx_reader,f]}
results={}
for label,args in commands.items():
 with (Q/(name+'-'+label+'.json')).open('x') as out,(Q/(name+'-'+label+'.stderr')).open('x') as err:
  results[label]=subprocess.run([sys.executable,*map(str,args)],cwd=R,stdout=out,stderr=err).returncode
m={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'parser_exit_codes':results};(Q/(name+'-pin.json')).write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m))
