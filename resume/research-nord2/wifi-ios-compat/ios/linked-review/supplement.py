from pathlib import Path
import struct,json,hashlib,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir());K=R/'research/ci-39744729444/xnu-kernel/kernel.development.sweet';T=R.parent/'toolchains/proton-clang/bin';D=K.read_bytes();out=P/'artifact';ex=json.loads((out/'extraction.json').read_text());assert hashlib.sha256(D).hexdigest()==ex['sha256']
segs=[];sections=[];off=32
for _ in range(struct.unpack_from('<I',D,16)[0]):
 cmd,n=struct.unpack_from('<II',D,off)
 if cmd==0x19:
  va,vs,fo,fs=struct.unpack_from('<QQQQ',D,off+24);segs.append((va,fo,fs))
  for i in range(struct.unpack_from('<I',D,off+64)[0]):
   s=off+72+80*i;name=D[s:s+16].split(b'\0')[0].decode();a,z=struct.unpack_from('<QQ',D,s+32);sections.append((name,a,z))
 off+=n
def read(a,n):
 hits=[(v,f) for v,f,z in segs if v<=a and a-v+n<=z];assert len(hits)==1;v,f=hits[0];return D[f+a-v:f+a-v+n]
raw=subprocess.check_output([str(T/'llvm-nm'),'-n','--demangle',str(K)],text=True);syms=[]
for l in raw.splitlines():
 q=l.split(maxsplit=2)
 if len(q)==3:
  try:syms.append((int(q[0],16),q[2]))
  except ValueError:pass
def address(n):
 a=[x for x,y in syms if y==n];assert len(a)==1,(n,a);return a[0]
meta=json.loads((out/'metaclasses-vtables.json').read_text());parents={'NiwlLifecycleClient':'IOUserClient','IONetworkInterface':'IOService','IO80211Interface':'IONetworkInterface','IO80211SkywalkInterface':'IO80211Interface','IO80211Controller':'IOService'}
for child,parent in parents.items():assert int(meta[child+'::superClass']['u64'],16)==address(parent+'::gMetaClass')
ctors={}
for label in ('GLOBAL__sub_I_wifi_ios_registry.cpp','GLOBAL__sub_I_wifi_ios_client.cpp'):
 target=int(ex['functions'][label]['address'],16);hits=[]
 for name,a,z in sections:
  if name=='__mod_init_func':
   assert z%8==0 and z<65536
   for i in range(z//8):
    if struct.unpack('<Q',read(a+8*i,8))[0]==target:hits.append(hex(a+8*i))
 assert len(hits)==1,(label,hits);ctors[label]={'address':hex(target),'actual_init_array_entry':hits[0]}
extra={}
for i,(a,n) in enumerate([(a,n) for a,n in syms if n=='IONetworkInterface::IONetworkInterface()']):
 end=min(x for x,_ in syms if x>a);text=subprocess.check_output([str(T/'llvm-objdump'),'-d','--demangle','--mattr=+lse','--no-show-raw-insn','--start-address='+hex(a),'--stop-address='+hex(end),str(K)],text=True);f=out/('network_ctor_'+str(i)+'.txt');f.write_text(text);extra[f.name]={'symbol':n,'address':hex(a),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
# Verify the descriptor virtual call offsets used by the actual linked client.
v=address('vtable for IOGeneralMemoryDescriptor');virtuals={}
for o,part in [(496,'getLength'),(528,'writeBytes'),(536,'prepare'),(544,'complete')]:
 target=struct.unpack('<Q',read(v+16+o,8))[0];names=[n for a,n in syms if a==target];assert any(part in n for n in names),(o,names);virtuals[str(o)]={'address':hex(target),'symbols':names}
strings={}
for a,name in [(0xfffffe000714a235,'IONetworkInterface'),(0xfffffe0007151965,'IO80211Interface'),(0xfffffe000715198c,'IO80211SkywalkInterface'),(0xfffffe00071519c1,'IO80211Controller')]:
 actual=read(a,64).split(b'\0')[0].decode();assert actual==name,(hex(a),actual);strings[name]=hex(a)
r={'verified_superclass_chain':parents,'constructors_retained_in_actual_init_array':ctors,'new_network_constructors':extra,'linked_descriptor_virtual_offsets':virtuals,'metaclass_name_strings':strings,'kernel_sha256':ex['sha256']};(out/'supplement.json').write_text(json.dumps(r,indent=2)+'\n');print('PASS: parent chain, actual constructor retention, descriptor virtual slots and class strings')
