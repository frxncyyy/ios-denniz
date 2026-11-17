from pathlib import Path
import subprocess,sys,hashlib,json,re
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').is_file());name=sys.argv[1];assert re.fullmatch(r'[a-z0-9-]+',name)
Q=P/'hardware-review';Q.mkdir(exist_ok=True);f=Q/(name+'.log');b=(Path(sys.argv[2]) if len(sys.argv)>2 else P/'hardware-live/xnu-live.log').read_bytes();f.open('xb').write(b)
commands={'backend':[P/'parse-backend.py',f],'ios':[P/'parse-ios-service.py',f],'connection':[P/'parse-connection.py',f],'wpa':[R/'tools/nord2-wifi-wpa-diag.py',f],'data':[R/'tools/nord2-wifi-data-diag.py',f,'--allow-prefix'],'extended':[P/'extended-reader/proposed/tools/nord2-wifi-data-extended.py',f]}
results={}
for label,args in commands.items():
 with (Q/(name+'-'+label+'.json')).open('x') as out,(Q/(name+'-'+label+'.stderr')).open('x') as err:
  results[label]=subprocess.run([sys.executable,*map(str,args)],cwd=R,stdout=out,stderr=err).returncode
m={'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest(),'parser_exit_codes':results};(Q/(name+'-pin.json')).write_text(json.dumps(m,indent=2)+'\n');print(json.dumps(m))
