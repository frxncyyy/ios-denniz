from pathlib import Path
import hashlib,json,subprocess
R=Path(__file__).resolve().parents[4];P=Path(__file__).resolve().parent;O=P/'focused';O.mkdir(exist_ok=True)
prior=json.loads((R/'research/nord2/wifi-ios-discovery/ios/focused/commands.json').read_text());base={x['name']:x for x in prior}
units=['wifi_ios_join','wifi_ios_station_view','wifi_station_wire','wifi_station_transport','wifi_station_transaction','wifi_wpa_crypto','wifi_wpa2','wifi_wpa_owner','wifi_wpa_nst','sweet_touch','wifi_ios_client','wifi_ios_registry']
results=[]
for name in units:
 cpp=name in ('wifi_ios_client','wifi_ios_registry');origin='wifi_ios_client' if cpp else 'sweet_touch' if name=='sweet_touch' else 'wifi_normal_transport';record=base[origin];cmd=record['argv'].copy()
 source=R/'research/ci-39158169212/buildtree/xnu'/('iokit/Kernel/arm' if cpp else 'bsd/dev')/(name+('.cpp' if cpp else '.c'))
 assert source.exists(),source
 index=next(i for i,x in enumerate(cmd) if x.endswith('/'+origin+('.cpp' if cpp else '.c')));cmd[index]=str(source)
 cmd[cmd.index('-o')+1]=str(O/(name+'.o'))
 if '-MF' in cmd:cmd[cmd.index('-MF')+1]=str(O/(name+'.d'))
 assert '-mcpu=generic' in cmd and '-march=armv8.2-a+crypto' in cmd
 with (O/(name+'.log')).open('wb') as log:r=subprocess.run(cmd,cwd=record['cwd'],stdout=log,stderr=subprocess.STDOUT)
 results.append(dict(name=name,argv=cmd,cwd=record['cwd'],source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),exit=r.returncode))
 (O/'commands.json').write_text(json.dumps(results,indent=2)+'\n')
 print(name,r.returncode,flush=True)
 if r.returncode:print((O/(name+'.log')).read_text()[-6000:],flush=True)
raise SystemExit(any(x['exit'] for x in results))
