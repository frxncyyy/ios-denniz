#!/usr/bin/env python3
"""Bounded cohesive first-join delta extraction; run after root verifies complete CI artifact."""
import argparse
import re
import hashlib
import json
from pathlib import Path
import struct
import subprocess

P=Path(__file__).resolve().parent
R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir())
ap=argparse.ArgumentParser(description=__doc__)
for option in ('kernel','sha256','bytes','run','source'):
 ap.add_argument('--'+option,required=True,type=int if option in ('bytes','run') else str)
ap.add_argument('--output',type=Path)
a=ap.parse_args();assert a.run>0 and 0<a.bytes<=64*1024*1024 and re.fullmatch(r'[0-9a-f]{40}',a.source) and re.fullmatch(r'[0-9a-f]{64}',a.sha256)
K=Path(a.kernel).resolve();assert K.stat().st_size==a.bytes;D=K.read_bytes()
pins=json.loads((P/'source-pins.json').read_text())['files']
for rel,wanted in pins.items():assert hashlib.sha256((R/rel).read_bytes()).hexdigest()==wanted,rel
assert len(D)==a.bytes and hashlib.sha256(D).hexdigest()==a.sha256
assert struct.unpack_from('<I',D)[0]==0xfeedfacf
T=R.parent/'toolchains/proton-clang/bin';segs=[];sections=[];o=32
for _ in range(struct.unpack_from('<I',D,16)[0]):
 cmd,z=struct.unpack_from('<II',D,o);assert z>=8 and o+z<=len(D)
 if cmd==0x19:
  va,vs,fo,fs=struct.unpack_from('<QQQQ',D,o+24);assert fo+fs<=len(D);segs.append((va,fo,fs))
  for j in range(struct.unpack_from('<I',D,o+64)[0]):
   s=o+72+j*80;name=D[s:s+16].split(b'\0')[0].decode();v,n=struct.unpack_from('<QQ',D,s+32);sections.append((name,v,n))
 o+=z
text=[(v,v+n)for s,v,n in sections if s=='__text']
raw=subprocess.check_output([str(T/'llvm-nm'),'-n','--demangle',str(K)],text=True);syms=[]
for line in raw.splitlines():
 q=line.split(maxsplit=2)
 if len(q)==3:
  try:syms.append((int(q[0],16),q[2]))
  except ValueError:pass

def read(v,n):
 hit=[(a,f)for a,f,z in segs if a<=v and v-a+n<=z];assert len(hit)==1
 a,f=hit[0];return D[f+v-a:f+v-a+n]

def istext(v):return any(a<=v<b for a,b in text)

def one(name):
 hits=[v for v,s in syms if s==name];assert len(hits)==1,(name,hits);return hits[0]

out=a.output or P/'artifact';assert not out.exists(),'Refuse overwrite';out.mkdir(parents=True);functions={}
patterns={
 'inventory':'_nord2_wifi_join_card_inventory',
 'client_get':'NiwlLifecycleClient::getLocked(',
 'client_join':'NiwlLifecycleClient::joinLocked(',
 'card_codec':'_niwc_card_reply',
 'join_decode':'_niwj_decode',
 'join_select':'_niwj_select',
 'join_enqueue':'_nord2_wifi_join_enqueue',
 'join_cancel':'_nord2_wifi_join_cancel',
 'same_worker':'_ncd_worker',
 'scan_deliver':'_nss_deliver',
 'join_early_finish':'_njs_early_finish',
 'native_prepare':'_nst_prepare',
 'native_decode':'_decode',
 'transaction_begin':'_stc_begin',
 'transaction_tick':'_stc_tick',
 'transaction_receipt':'_stc_receipt',
 'transaction_management':'_stc_management',
 'ap_rsn':'_nwp_ap_rsn',
 'ap_rsnxe':'_nwp_copy_ap_rsnxe',
 'wpa_receive':'_nwp_receive',
 'wpa_kde':'_kde',
 'ifnet_link':'_nord2_wifi_ifnet_link',
 'ifnet_event':'nwil_event(',
}
for label,pattern in patterns.items():
 hit=[(v,s)for v,s in syms if istext(v)and(s.startswith(pattern)if pattern.endswith('(')else s==pattern)and '.cold.'not in s];assert len(hit)==1,(label,hit)
 v,name=hit[0];end=min(x for x,_ in syms if x>v and istext(x));assert 0<end-v<(65536 if label=='same_worker'else 16384)
 f=out/(label+'.txt');f.write_text(subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--demangle','--start-address='+hex(v),'--stop-address='+hex(end),str(K)],text=True));functions[label]={'symbol':name,'address':hex(v),'end':hex(end),'code_bytes':end-v,'code_sha256':hashlib.sha256(read(v,end-v)).hexdigest(),'disassembly_sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
needles=[b'NORD2 WIFI IOS CARD: bytes=%u features=%u byte0=%#x byte1=%#x\n\0',b'mode=%u auth_lower=%u auth_upper=%u rsn_bytes=%u',b'rx_raw=%u rx_raw_eapol=%u rx_raw_drop=%u']
markers={}
for needle in needles:
 found=[]
 for name,v,n in sections:
  if name!='__cstring':continue
  data=read(v,n);pos=0
  while (pos:=data.find(needle,pos))>=0:found.append(hex(v+pos));pos+=len(needle)
 assert len(found)==1,(needle,found);markers[needle.decode().rstrip('\0')]=found[0]
expected=struct.pack('<13I',2,2,63,4,63,*([0]*8));constants=[]
for name,v,n in sections:
 if name not in('__const','__cstring'):continue
 data=read(v,n);pos=0
 while(pos:=data.find(expected,pos))>=0:constants.append({'section':name,'address':hex(v+pos),'bytes':52,'sha256':hashlib.sha256(expected).hexdigest()});pos+=len(expected)
# Compiler may directly materialize stores; constant absence is not refusal or
# proof. Manual instruction review must establish exact count/entries and zeros.
result={'status':'EXTRACTED_NOT_VERDICT','ci_run':a.run,'source':a.source,'kernel':str(K.relative_to(R)),'bytes':len(D),'sha256':a.sha256,'functions':functions,'diagnostic_addresses':markers,'possible_inventory_constant':constants,'expected_inventory_words':[2,2,63,4,63]+[0]*8,'source_sha256':pins,'limits':'23bounded delta bodies; static functions may inline into ncd_worker and are reviewed there. Exact addresses/instruction branches/calls require independent manual verdict. Markers/constants alone never prove behavior. Parent owns fullCI source andISA audits. No hardware/auth/IP success claim.'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('Extracted23bounded delta bodies; manualverdictpending')
