from pathlib import Path
import json,subprocess,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists());P=Path(__file__).parent;D=P/'proposed/shim/nord2-xnu';O=P/'native';O.mkdir(exist_ok=True)
b=next(x for x in json.loads((R/'research/nord2/wifi-ios-security/ios/focused/commands.json').read_text()) if x['name']=='wifi_ios_join');out=[]
for name in ['wifi_station_diag','wifi_station_transport','wifi_wpa_nst']:
 cmd=list(b['argv']);cmd[1:1]=['-I'+str(D)];cmd=[str(D/(name+'.c')) if x.endswith('/wifi_ios_join.c') else x for x in cmd];cmd[cmd.index('-o')+1]=str(O/(name+'.o'));z=subprocess.run(cmd,cwd=b['cwd'],capture_output=True,text=True);(O/(name+'.log')).write_text(z.stdout+z.stderr);out.append({'name':name,'argv':cmd,'cwd':b['cwd'],'returncode':z.returncode,'source_sha256':hashlib.sha256((D/(name+'.c')).read_bytes()).hexdigest()});print(name,z.returncode);z.check_returncode()
(O/'compile.json').write_text(json.dumps(out,indent=2)+'\n')
