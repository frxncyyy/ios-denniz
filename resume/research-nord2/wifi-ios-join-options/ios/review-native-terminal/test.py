#!/usr/bin/env python3
from pathlib import Path
import argparse,json,subprocess,tempfile,resource,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists())
ap=argparse.ArgumentParser();ap.add_argument('--baseline',action='store_true');args=ap.parse_args()
resource.setrlimit(resource.RLIMIT_CORE,(0,0))
out=P/('verification-baseline' if args.baseline else 'verification');out.mkdir(exist_ok=True)
S=R/'shim/nord2-xnu';F=R/'tools/fixtures/nord2-wifi-station';B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl'
names=['wifi_station_transaction','wifi_station_wire','wifi_wpa2','wifi_wpa_owner','wifi_wpa_crypto','wifi_wpa_nst','wifi_station_transport','wifi_normal_transport','wifi_capability','wifi_main_transport','wifi_main_command']
with tempfile.TemporaryDirectory(prefix='nst-terminal-') as td:
 d=Path(td)
 for f in S.iterdir():
  if f.suffix in ('.h','.c','.inc'):(d/f.name).write_bytes(f.read_bytes())
 if not args.baseline:
  for f in(P/'proposed/shim/nord2-xnu').iterdir():(d/f.name).write_bytes(f.read_bytes())
 files=[P/'test-timeout.c']+[d/(n+'.c') for n in names]+[B/'src'/f for f in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-const-variable','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(F),'-I'+str(B/'inc'),'-I'+str(B/'src'),*map(str,files),'-o',str(d/'test')]
 subprocess.run(cmd,check=True);v=subprocess.run([str(d/'test')],capture_output=True,text=True);(out/'test.log').write_text(v.stdout+v.stderr)
 report={'baseline':args.baseline,'exit':v.returncode,'hardware_claim':False,'sources':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'stdout':v.stdout,'stderr':v.stderr}
 (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(v.stdout or v.stderr[-1600:]);v.check_returncode()
