from pathlib import Path
import tempfile,subprocess,json,hashlib,runpy
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());F=R/'tools/fixtures/nord2-wifi-station';S=R/'shim/nord2-xnu';C=P/'proposed/shim/nord2-xnu';B=R/'research/ci-39158169212/buildtree/xnu/libkern/crypto/sweet_bearssl'
# Reuse public/synthetic independent Python hashlib/HMAC/cryptography producer,
# stopping before its compile/output section. No frozen package writes.
g={'__file__':str(P.parent/'wpa2-native-candidate/test.py')};text=(P.parent/'wpa2-native-candidate/test.py').read_text();exec(text[:text.index('W=P.parent/')],g)
rsn,gtk,ptk,msg=g['rsn'],g['gtk'],g['ptk'],g['msg'];rxse=bytes([244,1,32]);base=rsn+bytes.fromhex('dd16000fac010100')+gtk
def make(extra):
 q=base+extra;q+=b'\xdd'+b'\0'*((-len(q)-1)%8);return msg(0x13ca,2,aes_key_wrap(ptk[16:32],q))
v={'rxse':rxse,'good_rxse':make(rxse),'bad_rxse':make(bytes([244,1,64])),'duplicate_rxse':make(rxse+rxse),'unknown_rsn_kde':make(rxse+bytes.fromhex('dd04000fac99')),'vendor_m3':make(bytes.fromhex('dd0401020304'))}
ext=bytearray(rsn);ext[21]=32;q=bytes(ext)+bytes.fromhex('dd16000fac010100')+gtk;q+=b'\xdd'+b'\0'*((-len(q)-1)%8);v['extended_rsn']=bytes(ext);v['extended_m3']=msg(0x13ca,2,aes_key_wrap(ptk[16:32],q))
with tempfile.TemporaryDirectory(prefix='wpa-review-') as td:
 d=Path(td)
 for x in S.iterdir():
  if x.suffix in('.h','.c','.inc'):(d/x.name).write_bytes(x.read_bytes())
 for x in C.iterdir():(d/x.name).write_bytes(x.read_bytes())
 (d/'review-vectors.h').write_text('\n'.join('static const unsigned char '+k+'[]={'+','.join(map(str,b))+'};'for k,b in v.items()))
 files=[P/'proposed/tools/fixtures/nord2-wifi-station/test-review.c']+[d/(n+'.c')for n in ['wifi_wpa2','wifi_wpa_crypto','wifi_station_transaction','wifi_station_wire']]+[B/'src'/x for x in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-const-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(d),'-I'+str(F),'-I'+str(B/'inc'),'-I'+str(B/'src'),*map(str,files),'-o',str(d/'test')];subprocess.run(cmd,check=True);a=subprocess.run([str(d/'test')],text=True,capture_output=True);print(a.stdout,a.stderr);a.check_returncode();(P/'verification/focused.log').write_text(a.stdout+a.stderr)
