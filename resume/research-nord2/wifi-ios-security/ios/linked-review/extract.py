#!/usr/bin/env python3
"""Bounded installed CARD inventory and GET12 extraction; run after root verifies complete CI artifact."""
import argparse
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
a=ap.parse_args();K=Path(a.kernel).resolve();D=K.read_bytes()
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

out=P/'artifact';assert not out.exists(),'Refuse overwrite';out.mkdir();functions={}
for label,pattern in {'inventory':'_nord2_wifi_join_card_inventory','client_get':'NiwlLifecycleClient::getLocked(','card_codec':'_niwc_card_reply'}.items():
 hit=[(v,s)for v,s in syms if istext(v)and(s.startswith(pattern)if pattern.endswith('(')else s==pattern)and '.cold.'not in s];assert len(hit)==1,(label,hit)
 v,name=hit[0];end=min(x for x,_ in syms if x>v and istext(x));assert 0<end-v<8192
 f=out/(label+'.txt');f.write_text(subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--demangle','--start-address='+hex(v),'--stop-address='+hex(end),str(K)],text=True));functions[label]={'symbol':name,'address':hex(v),'end':hex(end),'code_bytes':end-v,'code_sha256':hashlib.sha256(read(v,end-v)).hexdigest(),'disassembly_sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
needle=b'NORD2 WIFI IOS CARD: bytes=%u features=%u byte0=%#x byte1=%#x\n\0';markers=[]
for name,v,n in sections:
 if name!='__cstring':continue
 data=read(v,n);pos=data.find(needle)
 if pos>=0:markers.append(hex(v+pos))
assert len(markers)==1,markers
expected=struct.pack('<13I',2,2,63,4,63,*([0]*8));constants=[]
for name,v,n in sections:
 if name not in('__const','__cstring'):continue
 data=read(v,n);pos=0
 while(pos:=data.find(expected,pos))>=0:constants.append({'section':name,'address':hex(v+pos),'bytes':52,'sha256':hashlib.sha256(expected).hexdigest()});pos+=len(expected)
# Compiler may directly materialize stores; constant absence is not refusal or
# proof. Manual instruction review must establish exact count/entries and zeros.
result={'status':'EXTRACTED_NOT_VERDICT','ci_run':a.run,'source':a.source,'kernel':str(K.relative_to(R)),'bytes':len(D),'sha256':a.sha256,'functions':functions,'card_marker_address':markers[0],'possible_inventory_constant':constants,'expected_inventory_words':[2,2,63,4,63]+[0]*8,'source_sha256':{n:hashlib.sha256((R/'shim/nord2-xnu'/n).read_bytes()).hexdigest()for n in ['wifi_ios_client.cpp','wifi_station_service.h','wifi_station_service_bind.inc','wifi_ios_card.h','wifi_ios_card.c']},'limits':'Three bounded bodies only. Actual GET12 inventory call before unchanged codec, count2/features2+4/ops63, rawbits3+14 and no connected-state gate require manual linked instruction review. No station/crypto/firmware re-audit or live proof.'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('Extracted3bounded bodies; manualverdictpending')
