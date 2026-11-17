#!/usr/bin/env python3
"""Bounded connection client/ifnet/owner extraction; run after root verifies complete CI artifact."""
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

out=P/'artifact';assert not out.exists(),'Refuse overwrite of prior linked extraction';out.mkdir();functions={}
patterns={
 'client_join':'NiwlLifecycleClient::joinLocked(',
 'client_callback':'NiwlLifecycleClient::joinCompleted(',
 'client_station_event':'NiwlLifecycleClient::stationEventLocked(',
 'client_get':'NiwlLifecycleClient::getLocked(',
 'client_discard':'NiwlLifecycleClient::discardLocked(',
 'client_dispatch':'NiwlLifecycleClient::dispatchLocked(',
 'client_external':'NiwlLifecycleClient::externalMethod(',
 'ifnet_link':'_nord2_wifi_ifnet_link',
 'ifnet_receive':'_nord2_wifi_ifnet_receive',
 'ifnet_output':'nwil_output(',
 'ifnet_ioctl':'nwil_ioctl(',
 'ifnet_event':'nwil_event(',
 'join_enqueue':'_nord2_wifi_join_enqueue',
 'join_cancel':'_nord2_wifi_join_cancel',
 'join_disconnect':'_nord2_wifi_join_disconnect',
 'join_snapshot':'_nord2_wifi_join_copy_state',
 'link_send':'_nord2_wifi_link_send',
 'same_worker':'_ncd_worker',
 'native_event':'_njs_event',
 'native_snapshot':'_njs_snapshot_locked',
 'native_notice':'_njs_notice',
 'join_decode':'_niwj_decode',
 'join_select':'_niwj_select',
 'state_authorized':'_niwj_authorized',
 'state_reply':'_niwj_state_reply',
 'station_notice_record':'_niwj_notice_record',
}
for label,pattern in patterns.items():
 hit=[(v,s)for v,s in syms if istext(v)and(s.startswith(pattern)if pattern.endswith('(')else s==pattern)and '.cold.'not in s];assert len(hit)==1,(label,hit)
 v,name=hit[0];end=min(x for x,_ in syms if x>v and istext(x));assert 0<end-v<32768
 f=out/(label+'.txt');dis=subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--demangle','--start-address='+hex(v),'--stop-address='+hex(end),str(K)],text=True);f.write_text(dis)
 calls=[]
 for line in dis.splitlines():
  match=__import__('re').search(r'\bbl\s+(0x[0-9a-fA-F]+)',line)
  if match:
   target=int(match[1],16);calls.append({'address':hex(target),'symbols':[s for x,s in syms if x==target]})
 functions[label]={'symbol':name,'address':hex(v),'end':hex(end),'code_bytes':end-v,'code_sha256':hashlib.sha256(read(v,end-v)).hexdigest(),'disassembly_sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'direct_calls':calls}
constants={}
for needle in (b'NORD2 WIFI IOS JOINREQ:',b'NORD2 WIFI IOS JOINNOTICE:',b'NORD2 WIFI IOS STATION:',b'NORD2 WIFI IOS STATIONGUARD:',b'NORD2 WIFI IFNET LINK:',b'NORD2 WIFI IOS LINK:'):
 hits=[]
 for name,v,n in sections:
  if name!='__cstring':continue
  data=read(v,n);pos=0
  while(pos:=data.find(needle,pos))>=0:
   end=data.find(b'\0',pos);assert end>=pos;hits.append({'address':hex(v+pos),'cstring':data[pos:end].decode()});pos+=len(needle)
 assert hits,needle;constants[needle.decode()]=hits
source_files=['wifi_ios_client.cpp','wifi_ios_registry.cpp','wifi_ios_join.c','wifi_ios_station_view.c','wifi_ifnet_link.inc','wifi_ifnet_link.h','wifi_station_service_bind.inc','wifi_scan_service_bind.inc']
result={'status':'EXTRACTED_NOT_VERDICT','run':a.run,'source':a.source,'kernel':str(K.relative_to(R)),'bytes':len(D),'sha256':a.sha256,'functions':functions,'constants':constants,'source_review_snapshot':{n:hashlib.sha256((R/'shim/nord2-xnu'/n).read_bytes()).hexdigest()for n in source_files},'method':'Bounded actual Mach-O code, direct calls and string locations. Source equality/generic ISA audits remain parent-owned. Manual instruction review required; this is not live association/IP proof.'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('Extracted',len(functions),'bounded actual bodies; verdict pending manual instruction review')
