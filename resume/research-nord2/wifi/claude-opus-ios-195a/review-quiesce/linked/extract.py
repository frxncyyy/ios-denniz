#!/usr/bin/env python3
"""Run only after root verifies CI artifact identity; bounded extraction, no verdict."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

P=Path(__file__).resolve().parent
R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir())
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--kernel',type=Path,required=True)
ap.add_argument('--sha256',required=True)
ap.add_argument('--bytes',type=int,required=True)
a=ap.parse_args()
K=a.kernel.resolve();D=K.read_bytes()
assert len(D)==a.bytes and hashlib.sha256(D).hexdigest()==a.sha256
assert struct.unpack_from('<I',D)[0]==0xfeedfacf
T=R.parent/'toolchains/proton-clang/bin'
segs=[];text=[];o=32
for _ in range(struct.unpack_from('<I',D,16)[0]):
 cmd,z=struct.unpack_from('<II',D,o);assert z>=8 and o+z<=len(D)
 if cmd==0x19:
  va,vs,fo,fs=struct.unpack_from('<QQQQ',D,o+24);assert fo+fs<=len(D);segs.append((va,fo,fs))
  for j in range(struct.unpack_from('<I',D,o+64)[0]):
   s=o+72+j*80
   if D[s:s+16].split(b'\0')[0]==b'__text':v,n=struct.unpack_from('<QQ',D,s+32);text.append((v,v+n))
 o+=z
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

def address(name):
 hit=[v for v,s in syms if s==name];assert len(hit)==1,(name,hit);return hit[0]

out=P/'artifact';out.mkdir(exist_ok=True)
functions={}
for label,name in [('matching','_StartIOKitMatching'),('daemon_ready','IOService::iokitDaemonLaunched()')]:
 v=address(name);end=min(x for x,_ in syms if x>v and istext(x));assert istext(v)and 0<end-v<2048
 file=out/(label+'.txt')
 file.write_text(subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--demangle','--start-address='+hex(v),'--stop-address='+hex(end),str(K)],text=True))
 functions[label]={'symbol':name,'address':hex(v),'end':hex(end),'code_sha256':hashlib.sha256(read(v,end-v)).hexdigest(),'file_sha256':hashlib.sha256(file.read_bytes()).hexdigest()}
# Resolve actual IOService virtual slots so a getBusyState call is not mistaken
# for adjustBusy, and capture only the relevant entries rather than all vtables.
vtable=address('vtable for IOService');table_bytes=min(v for v,_ in syms if v>vtable)-vtable
assert 16<table_bytes<8192
entries=[]
for off in range(16,table_bytes-7,8):
 target=struct.unpack('<Q',read(vtable+off,8))[0]
 names=[s for v,s in syms if v==target]
 if any(s.startswith(('IOService::getBusyState(', 'IOService::adjustBusy('))for s in names):
  entries.append({'vtable_address':hex(vtable),'offset_from_address_point':off-16,'pointer':hex(target),'symbols':names})
assert any('getBusyState' in str(x)for x in entries)and any('adjustBusy' in str(x)for x in entries)
result={'status':'EXTRACTED_NOT_VERDICT','run':39760685458,'source':'ee2e6fd9516b5eb7a5c41cabf1a077c93e53ccc7', 'kernel':str(K.relative_to(R)),'bytes':len(D),'sha256':a.sha256,'functions':functions,'busy_virtual_slots':entries,'method':'ordinary local LLVM and original Mach-O bytes; no runtime causality claim'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
