#!/usr/bin/env python3
"""Private composed offline tests; no writes outside requested evidence directory."""
from pathlib import Path
import tempfile,subprocess,json,hashlib,argparse
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if(x/'AGENTS.md').exists());B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl'
a=argparse.ArgumentParser();a.add_argument('--output',type=Path,default=P/'verification');args=a.parse_args();args.output.mkdir(parents=True,exist_ok=True)
overlays=[R/'shim/nord2-xnu',P.parent/'connect-review-wpa/proposed/shim/nord2-xnu',R/'research/nord2/wifi/claude-opus-connect-review/review-native/v2/candidate',P/'proposed/shim/nord2-xnu']
report={'hardware':False,'suites':[]}
with tempfile.TemporaryDirectory(prefix='join-admission-') as td:
 D=Path(td)
 for folder in overlays:
  for f in folder.iterdir():
   if f.suffix in ('.c','.h','.inc'):(D/f.name).write_bytes(f.read_bytes())
 suites={'join':['wifi_ios_join'],'service':['wifi_ios_join','wifi_wpa_nst','wifi_wpa_owner','wifi_station_transaction','wifi_station_wire','wifi_wpa2','wifi_wpa_crypto']}
 for name,names in suites.items():
  F=P/'proposed/tools/fixtures'/('nord2-wifi-ios-contract/test-join.c' if name=='join' else 'nord2-wifi-station/service-test.c')
  files=[F]+[D/(n+'.c') for n in names]
  if name=='service':files += [B/'src'/f for f in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
  cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-const-variable','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-pthread','-I'+str(D),'-I'+str(B/'inc'),'-I'+str(B/'src'),*map(str,files),'-o',str(D/name)]
  build=subprocess.run(cmd,capture_output=True,text=True);(args.output/(name+'.compile.log')).write_text(build.stdout+build.stderr);build.check_returncode()
  run=subprocess.run([str(D/name)],capture_output=True,text=True);(args.output/(name+'.log')).write_text(run.stdout+run.stderr);print(name,run.returncode,run.stdout.splitlines()[-1:] if run.stdout else run.stderr[-1500:]);run.check_returncode()
  report['suites'].append({'name':name,'status':'PASS','summary':run.stdout.splitlines()[-1],'inputs':{str(f if not f.is_relative_to(D) else Path('composed')/f.name):hashlib.sha256(f.read_bytes()).hexdigest() for f in files}})
 report['composed_sources']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in D.iterdir() if f.suffix in ('.c','.h','.inc')}
(args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
