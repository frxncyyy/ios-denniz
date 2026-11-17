#!/usr/bin/env python3
"""Bounded static extraction; parent verified artifact identity required."""
from pathlib import Path
import hashlib,json,struct,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir());T=R.parent/'toolchains/proton-clang/bin';K=R/'research/ci-39690505727/xnu-kernel/kernel.development.sweet'
D=K.read_bytes();SHA='820a1879f676177633ca877506a60ea0a06cba6ccfbca87d53cd5dada71e99f0'
assert len(D)==17603624 and hashlib.sha256(D).hexdigest()==SHA and struct.unpack_from('<I',D)[0]==0xfeedfacf
segments=[];text=[];off=32
for _ in range(struct.unpack_from('<I',D,16)[0]):
 cmd,n=struct.unpack_from('<II',D,off);assert n>=8 and off+n<=len(D)
 if cmd==0x19:
  name=D[off+8:off+24].split(b'\0')[0].decode();va,vs,fo,fs=struct.unpack_from('<QQQQ',D,off+24);segments.append((va,fo,fs,name));assert fo+fs<=len(D)
  for j in range(struct.unpack_from('<I',D,off+64)[0]):
   s=off+72+j*80
   if D[s:s+16].split(b'\0')[0]==b'__text':a,z=struct.unpack_from('<QQ',D,s+32);text.append((a,a+z))
 off+=n
raw=subprocess.check_output([str(T/'llvm-nm'),'-n','--demangle',str(K)],text=True);syms=[]
for line in raw.splitlines():
 q=line.split(maxsplit=2)
 if len(q)==3:
  try:syms.append((int(q[0],16),q[1],q[2]))
  except ValueError:pass
istext=lambda a:any(x<=a<y for x,y in text)
def read(a,n):
 s=[(va,fo) for va,fo,fs,_ in segments if va<=a and a-va+n<=fs];assert len(s)==1
 va,fo=s[0];return D[fo+a-va:fo+a-va+n]
out=P/'artifact';out.mkdir(exist_ok=True);selected={}
want=['_ncd_worker','_nss_deliver','_nss_build','_nwm_failure_drain','_nord2_wifi_scan_info','_nord2_wifi_scan_enqueue','_nord2_wifi_scan_cancel','_nord2_wifi_scan_view','_nord2_wifi_scan_release','_nct_done_receipt','_nct_continue_prepare','_nct_lease_window','_nct_run','_ncr_continue_prepare','_ncr_pump','_niwi_scan_envelope','_niwi_scan_admit','_niwi_rate_map','_niwr_production_map','_niwl_create_client','_nord2_wifi_ios_register','_nord2_wifi_ios_backend_ready','__GLOBAL__sub_I_wifi_ios_client.cpp','__GLOBAL__sub_I_wifi_ios_registry.cpp']
cpp=[('client_init','NiwlLifecycleClient::initWithTask('),('client_close','NiwlLifecycleClient::closeOwned('),('client_dispatch','NiwlLifecycleClient::dispatchLocked('),('client_scan','NiwlLifecycleClient::scanLocked('),('client_get','NiwlLifecycleClient::getLocked('),('client_external','NiwlLifecycleClient::externalMethod('),('client_memory','NiwlLifecycleClient::clientMemoryForType('),('client_free','NiwlLifecycleClient::free('),('client_complete','NiwlLifecycleClient::completed('),('client_event','NiwlLifecycleClient::eventLocked('),('registry_start','IO80211Controller::start('),('registry_ioctl','IO80211Controller::ioctl('),('registry_stop','IO80211Controller::stop('),('registry_output','IO80211Controller::output('),('interface_newclient','IO80211Interface::newUserClient(')]
for label,pattern,exact in [(n.lstrip('_'),n,True) for n in want]+[(a,b,False) for a,b in cpp]:
 hits=[(a,n) for a,_,n in syms if istext(a) and (n==pattern if exact else n.startswith(pattern))];assert len(hits)==1,(pattern,hits)
 a,name=hits[0];end=min(x for x,_,_ in syms if x>a and istext(x));assert end-a<65536
 txt=subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--no-show-raw-insn','--demangle','--start-address='+hex(a),'--stop-address='+hex(end),str(K)],text=True)
 f=out/(label+'.txt');f.write_text(txt);selected[label]={'symbol':name,'address':hex(a),'end':hex(end),'file':f.name,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
data={}
for cls in ('NiwlLifecycleClient','IO80211Interface','IO80211SkywalkInterface','IO80211Controller'):
 for suffix in ('::metaClass','::superClass','::gMetaClass'):
  hits=[a for a,_,n in syms if n==cls+suffix];assert len(hits)==1;addr=hits[0]
  try:b=read(addr,8);content={'u64':hex(struct.unpack('<Q',b)[0])}
  except AssertionError:content={'zero_fill':True}
  data[cls+suffix]={'address':hex(addr),**content}
 names=[(a,n) for a,_,n in syms if n=='vtable for '+cls];assert len(names)==1;a,name=names[0]
 nextaddr=min(x for x,_,_ in syms if x>a);n=nextaddr-a;assert n<4096;body=read(a,n);words=struct.unpack('<'+'Q'*(n//8),body[:n//8*8]);ptrs=[]
 for i,v in enumerate(words):
  targets=[n for x,_,n in syms if x==v]
  if targets:ptrs.append({'offset':8*i,'target':hex(v),'symbols':targets})
 data[name]={'address':hex(a),'bytes':n,'sha256':hashlib.sha256(body).hexdigest(),'targets':ptrs}
(out/'metaclasses-vtables.json').write_text(json.dumps(data,indent=2)+'\n')
result={'status':'EXTRACTED_NOT_VERDICT','run':39690505727,'source':'52bb4e7b9c1e7cf5cfab3a72e9d608ccf04368dd','kernel':str(K),'bytes':len(D),'sha256':SHA,'functions':selected,'method':'ordinary local LLVM/Mach-O static analysis, not runtime IPC evidence'}
(out/'extraction.json').write_text(json.dumps(result,indent=2)+'\n');print('extracted',len(selected),'bounded functions')
