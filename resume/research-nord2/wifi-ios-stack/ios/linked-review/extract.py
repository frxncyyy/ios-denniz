#!/usr/bin/env python3
"""Bounded stack-only extraction; run after root verifies complete CI artifact."""
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

out=P/'artifact';out.mkdir(exist_ok=True);functions={}
patterns={
 'controller_start':'IO80211Controller::start(',
 'controller_stop':'IO80211Controller::stop(',
 'stack_init_owned':'IONetworkStack::initOwned(',
 'stack_live':'IONetworkStack::liveLocked(',
 'stack_newclient':'IONetworkStack::newUserClient(',
 'stack_set':'IONetworkStack::setProperties(',
 'stack_start':'IONetworkStack::start(',
 'stack_stop':'IONetworkStack::stop(',
 'stack_free':'IONetworkStack::free(',
 'client_init':'IONetworkStackUserClient::initWithTask(',
 'client_start':'IONetworkStackUserClient::start(',
 'client_set':'IONetworkStackUserClient::setProperties(',
 'client_close':'IONetworkStackUserClient::clientClose(',
 'client_died':'IONetworkStackUserClient::clientDied(',
 'client_stop':'IONetworkStackUserClient::stop(',
 'client_free':'IONetworkStackUserClient::free(',
 'registry_constructor':'__GLOBAL__sub_I_wifi_ios_registry.cpp',
}
for label,pattern in patterns.items():
 hit=[(v,s)for v,s in syms if istext(v)and s.startswith(pattern)and '.cold.'not in s];assert len(hit)==1,(label,hit)
 v,name=hit[0];end=min(x for x,_ in syms if x>v and istext(x));assert 0<end-v<16384
 f=out/(label+'.txt');f.write_text(subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--demangle','--start-address='+hex(v),'--stop-address='+hex(end),str(K)],text=True))
 functions[label]={'symbol':name,'address':hex(v),'end':hex(end),'code_sha256':hashlib.sha256(read(v,end-v)).hexdigest(),'disassembly_sha256':hashlib.sha256(f.read_bytes()).hexdigest()}

metadata={}
for cls,parent in [('IONetworkStack','IOService'),('IONetworkStackUserClient','IOUserClient')]:
 superclass=one(cls+'::superClass');pointer=struct.unpack('<Q',read(superclass,8))[0]
 # superClass stores the actual base metaClass pointer, not a class name.
 expected=struct.unpack('<Q',read(one(parent+'::metaClass'),8))[0]
 assert pointer==expected,(cls,hex(pointer),hex(expected))
 v=one('vtable for '+cls);end=min(x for x,_ in syms if x>v);assert 16<end-v<8192
 entries=[]
 for off in range(16,end-v-7,8):
  target=struct.unpack('<Q',read(v+off,8))[0];names=[s for x,s in syms if x==target]
  if any(s.startswith(cls+'::')for s in names):entries.append({'offset_from_address_point':off-16,'pointer':hex(target),'symbols':names})
 metadata[cls]={'superClass_pointer':hex(pointer),'expected_base':parent,'vtable':hex(v),'selected_virtual_targets':entries}
constructor=int(functions['registry_constructor']['address'],16);init=[]
for name,v,n in sections:
 if name=='__mod_init_func':
  assert n%8==0 and n<65536
  for off in range(0,n,8):
   if struct.unpack('<Q',read(v+off,8))[0]==constructor:init.append(hex(v+off))
assert len(init)==1,init
constants={}
for needle in (b'IONetworkStackUserClient\0',b'IONetworkStackUserCommand\0',b'IORegistryEntryID\0',b'IOInterfaceUnit\0',b'IOInterfaceState\0',b'NORD2 WIFI IOS STACK: registered=1',b'NORD2 WIFI IOS STACK OPEN:',b'NORD2 WIFI IOS STACK SET:'):
 hits=[]
 for name,v,n in sections:
  if name!='__cstring':continue
  data=read(v,n);pos=0
  while (pos:=data.find(needle,pos))>=0:
   end=data.find(b'\0',pos);assert end>=pos;hits.append({'address':hex(v+pos),'cstring':data[pos:end].decode()});pos+=len(needle)
 assert hits,needle;constants[needle.rstrip(b'\0').decode()]=hits
result={'status':'EXTRACTED_NOT_VERDICT','run':a.run,'source':a.source,'kernel':str(K.relative_to(R)),'bytes':len(D),'sha256':a.sha256,'functions':functions,'metaclasses':metadata,'actual_registry_init_array_entry':init[0],'constants':constants,'method':'ordinary local LLVM/original Mach-O; no live initialization evidence'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('Extracted',len(functions),'bounded bodies, two new metaclasses/vtables and constructor registration; verdict pending manual inspection')
