from pathlib import Path
import tempfile,subprocess
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'AGENTS.md').exists());S=R/'shim/nord2-xnu';B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl';A=P.parent
with tempfile.TemporaryDirectory() as td:
 d=Path(td)
 for folder in [S,A/'proposed/shim/nord2-xnu']:
  for f in folder.iterdir():
   if f.suffix in ('.c','.h','.inc'):(d/f.name).write_bytes(f.read_bytes())
 names=['wifi_station_data_diag','wifi_station_diag','wifi_wpa_diag','wifi_station_transaction','wifi_station_wire','wifi_wpa2','wifi_wpa_owner','wifi_wpa_crypto','wifi_wpa_nst','wifi_station_transport','wifi_normal_transport','wifi_capability','wifi_main_transport','wifi_main_command']
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-Wno-unused-const-variable','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all',*['-I'+str(x) for x in [d,A/'proposed/tools/fixtures/nord2-wifi-data-diag',R/'tools/fixtures/nord2-wifi-station',B/'inc',B/'src']],str(P/'probe.c'),*[str(d/(x+'.c')) for x in names],*[str(B/'src'/x) for x in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']],'-o',str(d/'probe')];subprocess.run(cmd,check=True);v=subprocess.run([str(d/'probe')],capture_output=True,text=True);(P/'probe.log').write_text(v.stdout+v.stderr);print(v.returncode,v.stdout[-350:],v.stderr[-800:]);v.check_returncode()
