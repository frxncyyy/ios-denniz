from pathlib import Path
import hashlib,json,struct,subprocess
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());A=P/'artifact';X=json.loads((A/'extraction.json').read_text());K=Path(X['kernel']);b=K.read_bytes();assert hashlib.sha256(b).hexdigest()==X['sha256']=='ce122a75228da792a25c2fc448d9bfd7904fe751f4b1d976a9079936e19a6e2d'
T=R.parent/'toolchains/proton-clang/bin';nm=subprocess.check_output([str(T/'llvm-nm'),'-n',str(K)],text=True);syms=[]
for line in nm.splitlines():
 q=line.split()
 if len(q)==3:
  try:syms.append((int(q[0],16),q[2]))
  except ValueError:pass
out={}
for name in ['_nvc_after_persistent.cold.1','_nvc_after_persistent.cold.2','_nvc_observe.cold.1','_OUTLINED_FUNCTION_2','_OUTLINED_FUNCTION_0']:
 hits=[x for x,n in syms if n==name];va={'_OUTLINED_FUNCTION_2':0xfffffe0007515574,'_OUTLINED_FUNCTION_0':0xfffffe0007515560}.get(name,hits[0]);assert va in hits;end=min(x for x,n in syms if x>va);assert end-va<256
 text=subprocess.check_output([str(T/'llvm-objdump'),'-d','--mattr=+lse','--no-show-raw-insn','--start-address='+hex(va),'--stop-address='+hex(end),str(K)],text=True)
 f=A/(name[1:]+'.txt');f.write_text(text);out[name]={'address':hex(va),'end':hex(end),'sha256':hashlib.sha256(f.read_bytes()).hexdigest()}
def vm(va,n):
 hit=[s for s in X['segments'] if s['address']<=va and va-s['address']+n<=s['file_bytes']];assert len(hit)==1
 s=hit[0];return b[s['offset']+va-s['address']:s['offset']+va-s['address']+n]
for va in [0xfffffe000702f3ec,0xfffffe000700c644]:
 values=struct.unpack('<5H',vm(va,10));assert values==(0x1f98,0x1f9c,0x1be8,0x1be8,0x1c0e);out[hex(va)]={'actual_five_slot_addresses':[hex(x) for x in values]}
c=json.loads((A/'constants.json').read_text());assert c['_nvc_ops']['function_pointers']==[X['functions'][name]['address'] for name in ['_nvc_owner','_nvc_read','_nvc_write','_nvc_rc']]
# Exact in-file strings used at the three actual boot-argument references.
for va,want in [(0xfffffe000709585a,b'nord2wifivcn33'),(0xfffffe0007095077,b'nord2wifimain'),(0xfffffe0007095654,b'nord2wifipersist')]:
 assert vm(va,len(want)+1)==want+b'\0';out[hex(va)]={'boot_argument':want.decode()}
recipe=struct.unpack('<6Q',vm(0xfffffe0007217600,48));assert recipe[1]==26 and recipe[4:]==(60000000,15000000);out['actual_recipe']={'address':'0xfffffe0007217600','words':[hex(x) for x in recipe]}
out['parent_audits']={str(f.relative_to(R)):hashlib.sha256(f.read_bytes()).hexdigest() for f in [R/'research/ci-39684479974/source-buildtree-audit.json',R/'research/ci-39684479974/normal-arm64-audit.json']}
(A/'supplement.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
