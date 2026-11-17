from pathlib import Path
import struct,json,hashlib,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir());K=R/'research/ci-39690505727/xnu-kernel/kernel.development.sweet';T=R.parent/'toolchains/proton-clang/bin';D=K.read_bytes();assert hashlib.sha256(D).hexdigest()=='820a1879f676177633ca877506a60ea0a06cba6ccfbca87d53cd5dada71e99f0'
segs=[];sections=[];o=32
for _ in range(struct.unpack_from('<I',D,16)[0]):
 c,n=struct.unpack_from('<II',D,o)
 if c==0x19:
  va,vs,fo,fs=struct.unpack_from('<QQQQ',D,o+24);segs.append((va,fo,fs))
  for i in range(struct.unpack_from('<I',D,o+64)[0]):
   s=o+72+i*80;name=D[s:s+16].split(b'\0')[0].decode();a,z=struct.unpack_from('<QQ',D,s+32);sections.append((name,a,z))
 o+=n
def read(a,n):
 hit=[(v,f) for v,f,z in segs if v<=a and a-v+n<=z];assert len(hit)==1;v,f=hit[0];return D[f+a-v:f+a-v+n]
raw=subprocess.check_output([str(T/'llvm-nm'),'-n','--demangle',str(K)],text=True);syms=[]
for l in raw.splitlines():
 q=l.split(maxsplit=2)
 if len(q)==3:
  try:syms.append((int(q[0],16),q[2]))
  except ValueError:pass
out=P/'artifact';extra={}
for label,start in [('native_owner',0xfffffe00075111a8),('normal_owner_window',0xfffffe0007510d34),('checked_pump',0xfffffe0007994574)]:
 end=min(a for a,_ in syms if a>start);txt=subprocess.check_output([str(T/'llvm-objdump'),'-d','--demangle','--mattr=+lse','--no-show-raw-insn','--start-address='+hex(start),'--stop-address='+hex(end),str(K)],text=True);f=out/(label+'.txt');f.write_text(txt);extra[label]={'address':hex(start),'end':hex(end),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
ctor={}
for name,target in [('client',0xfffffe0007acfe50),('registry',0xfffffe0007ad0dcc)]:
 hits=[]
 for section,a,z in sections:
  if section=='__mod_init_func':
   assert z%8==0 and z<65536
   for i in range(z//8):
    if struct.unpack('<Q',read(a+8*i,8))[0]==target:hits.append(hex(a+8*i))
 assert len(hits)==1,(name,hits);ctor[name]={'constructor':hex(target),'actual_mod_init_entry':hits[0]}
meta=json.loads((out/'metaclasses-vtables.json').read_text());parents={'NiwlLifecycleClient':'0xfffffe0007c82628','IO80211Interface':'0xfffffe0007c796b8','IO80211SkywalkInterface':'0xfffffe0007c83478','IO80211Controller':'0xfffffe0007c796b8'}
for name,want in parents.items():assert meta[name+'::superClass']['u64']==want
constants={}
for name,a,n in [('media_values',0xfffffe0007037830,16),('production_rates',0xfffffe000702ebf8,96)]:
 b=read(a,n);constants[name]={'address':hex(a),'bytes':n,'hex':b.hex(),'sha256':hashlib.sha256(b).hexdigest()}
assert struct.unpack('<4I',read(0xfffffe0007037830,16))==(128,0,1,128)
constants['media_values']['decoded']={'current':128,'mask':0,'status_AVALID':1,'active_type_AUTO':128,'carrier_ACTIVE':False}
# Mapping struct layout is uint8 IEcode then3padding bytes then uint32 Applecode.
rates=[(read(0xfffffe000702ebf8+8*i,1)[0],struct.unpack('<I',read(0xfffffe000702ebf8+8*i+4,4))[0]) for i in range(12)]
assert rates==list(zip([2,4,11,22,12,18,24,36,48,72,96,108],[1,2,5,11,6,9,12,18,24,36,48,54])),rates
constants['production_rates']['decoded_compatibility_policy']=rates
result={'constructors_in_actual_init_array':ctor,'parents_verified':parents,'extra_functions':extra,'constants':constants};(out/'supplement.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'constructors':ctor,'rates':rates,'media':'PASS'}))
