#!/usr/bin/env python3
from pathlib import Path
import argparse,subprocess,tempfile,json,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;P=D/'proposed';a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);a.add_argument('--only',choices=['codec','actual','owner','owner-reverse','regressions'],default='codec');x=a.parse_args();x.output.mkdir(parents=True,exist_ok=False)
before={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (P/'shim/nord2-xnu').iterdir()}
with tempfile.TemporaryDirectory() as z:
 T=Path(z)
 for S in [R/'shim/nord2-xnu',P/'shim/nord2-xnu']:
  for p in S.iterdir():
   if p.suffix in ('.c','.h','.inc'):(T/p.name).write_bytes(p.read_bytes())
 if x.only=='codec':
  names=['wifi_ios_join','wifi_ios_station_view','wifi_ios_policy','wifi_ios_mapping','wifi_ios_rates','wifi_ios_scan_contract','wifi_station_wire','wifi_station_tx'];fixture=P/'tools/fixtures/nord2-wifi-open/test-codec.c';cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(T),str(fixture),*[str(T/(n+'.c')) for n in names],'-o',str(T/'test')]
  c=subprocess.run(cmd,capture_output=True,text=True);(x.output/'compile.log').write_text(c.stdout+c.stderr);c.check_returncode();r=subprocess.run([str(T/'test')],capture_output=True,text=True);(x.output/'test.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout,end='')
 elif x.only=='regressions':
  r=subprocess.run(['python3',str(R/'tools/test-nord2-wifi-station.py'),'--candidate',str(P/'shim/nord2-xnu'),'--log-dir',str(x.output/'station')],capture_output=True,text=True);(x.output/'runner.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout,end='')
 elif x.only=='actual':
  B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl';F=R/'tools/fixtures/nord2-wifi-station';fixture=P/'tools/fixtures/nord2-wifi-open/test-actual.c';names=['wifi_wpa_diag','wifi_station_data_diag','wifi_station_diag','wifi_station_transaction','wifi_station_wire','wifi_wpa2','wifi_wpa_owner','wifi_wpa_crypto','wifi_wpa_nst','wifi_station_tx','wifi_station_transport','wifi_normal_transport','wifi_capability','wifi_main_transport','wifi_main_command'];extra=[B/'src'/n for n in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']];cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-function','-Wno-unused-const-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(p) for p in [T,F,B/'inc',B/'src']],str(fixture),*[str(T/(n+'.c')) for n in names],*map(str,extra),'-o',str(T/'test')]
  c=subprocess.run(cmd,capture_output=True,text=True);(x.output/'compile.log').write_text(c.stdout+c.stderr);c.check_returncode();r=subprocess.run([str(T/'test')],capture_output=True,text=True);(x.output/'test.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout.splitlines()[-1])
 elif x.only in ('owner','owner-reverse'):
  r=subprocess.run(['python3',str(D/'run-owner.py'),'--candidate',str(P/'shim/nord2-xnu'),'--log-dir',str(x.output/'owning')]+(['--initial-open','0'] if x.only=='owner-reverse' else []),capture_output=True,text=True);(x.output/'runner.log').write_text(r.stdout+r.stderr);r.check_returncode();print(r.stdout,end='')
 else:raise SystemExit('fixture not yet prepared')
 assert before=={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (P/'shim/nord2-xnu').iterdir()},'candidate source changed during test'
 (x.output/'report.json').write_text(json.dumps({'passed':True,'hardware_claim':False,'group':x.only,'sources':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (P/'shim/nord2-xnu').iterdir()}},indent=2)+'\n')
