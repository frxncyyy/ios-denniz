from pathlib import Path
import hashlib,hmac,struct,tempfile,subprocess
from cryptography.hazmat.primitives.keywrap import aes_key_wrap
P=Path(__file__).resolve().parent
R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());B=R/'research/ci-39398655023/buildtree/xnu/libkern/crypto/sweet_bearssl'
# Public IEEE password/IEEE PMK example plus independent Python standard-library
# and cryptography oracle. All identities/keys below are synthetic, never live.
pmk=hashlib.pbkdf2_hmac('sha1',b'password',b'IEEE',4096,32)
assert pmk.hex()=='f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e'
own=bytes.fromhex('020000000001');peer=bytes.fromhex('020000000002');sn=bytes(range(1,33));an=bytes(range(33,65))
rsn=bytes.fromhex('30140100000fac040100000fac040100000fac020000')
d=min(own,peer)+max(own,peer)+min(sn,an)+max(sn,an)
ptk=b''.join(hmac.new(pmk,b'Pairwise key expansion\0'+d+bytes([i]),'sha1').digest() for i in range(3))[:48]
gtk=bytes(range(64,80));plain=rsn+bytes.fromhex('dd16000fac010100')+gtk
plain+=b'\xdd'+b'\0'*((-len(plain)-1)%8)
wrapped=aes_key_wrap(ptk[16:32],plain)
def msg(info,replay,data=b''):
 p=bytearray(99+len(data));p[0]=2;p[1]=3;p[2:4]=struct.pack('>H',len(p)-4);p[4]=2;p[5:9]=struct.pack('>HH',info,16);p[9:17]=replay.to_bytes(8,'big');p[17:49]=an;p[97:99]=struct.pack('>H',len(data));p[99:]=data
 if info&0x100:p[81:97]=hmac.new(ptk[:16],p,'sha1').digest()[:16]
 return bytes(p)
m1=msg(0x8a,1);m3=msg(0x13ca,2,wrapped)
def answer(info,data=b'',nonce=b''):
 p=bytearray(99+len(data));p[0]=2;p[1]=3;p[2:4]=struct.pack('>H',len(p)-4);p[4]=2;p[5:7]=struct.pack('>H',info);p[9:17]=(1 if data else 2).to_bytes(8,'big');p[17:17+len(nonce)]=nonce;p[97:99]=struct.pack('>H',len(data));p[99:]=data;p[81:97]=hmac.new(ptk[:16],p,'sha1').digest()[:16];return bytes(p)
v={'m2_oracle':answer(0x10a,rsn,sn),'m4_oracle':answer(0x30a),'pmk_vector':pmk,'own':own,'peer':peer,'sn':sn,'rsn':rsn,'ptk':ptk,'gtk':gtk,'m1':m1,'m3':m3,
'rfc_kek':bytes.fromhex('000102030405060708090a0b0c0d0e0f'),
'rfc_wrapped':bytes.fromhex('1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5'),
'rfc_plain':bytes.fromhex('00112233445566778899aabbccddeeff')}
# Authenticated malformed KDE/RSN/nonce/replay variants, independent producer.
for name,modify in [('bad_rsn',lambda x:x.__setitem__(8,2)),('duplicate_gtk',None),('bad_kde_length',lambda x:x.__setitem__(23,23))]:
 q=bytearray(plain)
 if modify:modify(q)
 else:q=bytearray(rsn+bytes.fromhex('dd16000fac010100')+gtk+bytes.fromhex('dd16000fac010100')+gtk+b'\xdd\0')
 while len(q)%8:q+=b'\0'
 v[name]=msg(0x13ca,2,aes_key_wrap(ptk[16:32],bytes(q)))
W=P.parent/'station-wire-candidate';F=P.parent/'wpa2-engine-candidate/followup'
with tempfile.TemporaryDirectory(prefix='stc-model-') as td:
 t=Path(td);(t/'vectors.h').write_text('\n'.join('static const unsigned char '+k+'[]={'+','.join(map(str,b))+'};' for k,b in v.items()))
 files=[P/'test.c',P/'transaction.c',W/'station_wire.c',F/'replacement/wifi_wpa2.c',F/'crypto_provider.c']
 files += [B/'src'/x for x in ['hash/sha1.c','codec/dec32be.c','codec/enc32be.c','symcipher/aes_ct.c','symcipher/aes_ct_dec.c','symcipher/aes_ct_cbcdec.c']]
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-const-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(W),'-I'+str(F/'replacement'),'-I'+str(B/'inc'),'-I'+str(B/'src'),'-I'+td]+list(map(str,files))+['-o',td+'/test'];subprocess.run(cmd,check=True);subprocess.run([td+'/test'],check=True)
