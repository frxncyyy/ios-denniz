#!/usr/bin/env python3
"""Bounded linked evidence extraction ONLY after parent confirms verified artifact.

No download, CI query, compile, device operation or stage gate mutation.
This script produces evidence, never a PASS verdict. The flag alone is not
parent authorization; wait for the explicit verified artifact/path/hash message.
"""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess
OUT=Path(__file__).resolve().parent
ROOT=OUT.parents[3]
MANDATORY=('nbc_prepare','nbc_lease_window','nbc_run','nnr_plan','nnr_begin',
 'nnr_reserve_once','nni_basic','nbc_caps_decode','ncd_worker','nwfn_guard',
 'ncd_native_owner','ncd_native_now','ncd_native_read','ncd_native_write',
 'ncd_native_mread','ncd_native_mwrite','ncd_native_sync','ncd_native_delay',
 'nwm_failure_drain')
OPTIONAL=('nbc_after_cap','nbc_report','ncd_normal_owner_window')
def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--artifact-verified',action='store_true')
 ap.add_argument('--ci-run',required=True,type=int)
 ap.add_argument('--source-commit',required=True)
 ap.add_argument('--kernel-path',required=True)
 ap.add_argument('--run-json',required=True)
 ap.add_argument('--kernel-sha256',required=True)
 args=ap.parse_args()
 if not args.artifact_verified:ap.error('WAIT: parent must explicitly confirm artifact before invocation')
 if args.ci_run<=0 or not re.fullmatch('[0-9a-f]{40}',args.source_commit) or not re.fullmatch('[0-9a-f]{64}',args.kernel_sha256):ap.error('exact verified identity required')
 plan=json.loads((OUT/'linked-basic-plan.json').read_text())
 if not args.source_commit.startswith(plan['source_commit_prefix']):raise SystemExit('STOP: unexpected source prefix')
 def relative(s):
  p=Path(s)
  if p.is_absolute() or '..' in p.parts or not s.startswith(f'research/ci-{args.ci_run}/'):raise SystemExit('STOP: CI relative path mismatch')
  return ROOT/p
 kernel=relative(args.kernel_path);metadata=relative(args.run_json)
 # No artifact read before all invocation gates and path checks above.
 run=json.loads(metadata.read_text())
 if (run.get('headSha'),run.get('status'),run.get('conclusion'))!=(args.source_commit,'completed','success'):raise SystemExit('STOP: exact CI source/outcome mismatch')
 data=kernel.read_bytes();digest=hashlib.sha256(data).hexdigest()
 if digest!=args.kernel_sha256:raise SystemExit('STOP: kernel SHA mismatch')
 for p,wanted in plan['source_pins'].items():
  if hashlib.sha256((ROOT/p).read_bytes()).hexdigest()!=wanted:raise SystemExit('STOP: source drift '+p)
 obj=ROOT/'../toolchains/proton-clang/bin/llvm-objdump';nm=ROOT/'../toolchains/proton-clang/bin/llvm-nm'
 raw=subprocess.check_output([str(nm),'-n',str(kernel)],text=True)
 symbols=[]
 for line in raw.splitlines():
  m=re.fullmatch(r'([0-9a-fA-F]{16}) ([A-Za-z]) (.+)',line)
  if m:symbols.append((int(m[1],16),m[2],m[3]))
 def find(n,required=True):
  a=[v for v,_,name in symbols if name=='_'+n]
  if len(a)==1:return a[0]
  if not a and not required:return None
  raise ValueError('ambiguous/missing '+n)
 addresses={n:find(n) for n in MANDATORY}
 optional={n:find(n,False) for n in OPTIONAL}
 def dump(*flags):
  text=subprocess.check_output([str(obj),'-d','--mattr=+lse','--no-show-raw-insn',*flags,str(kernel)],text=True)
  if '<unknown>' in text:raise ValueError('undecoded targeted instructions')
  if len(text)>350000:raise ValueError('targeted output bound exceeded')
  return text
 def save(name,value):
  b=value.encode()
  p=OUT/name
  if p.exists() and p.read_bytes()!=b:raise ValueError('preserve prior evidence explicitly: '+name)
  if not p.exists():p.write_bytes(b)
 core=dump('--disassemble-symbols='+','.join('_'+n for n in (*MANDATORY,*[x for x,v in optional.items() if v is not None])))
 save('linked-basic-core-disassembly.txt',core)
 # Static helpers may share names with the old CAP TU. Bound the NEW basic
 # TU by its three public functions and the next public helper TU, not names.
 first=min(addresses[x] for x in ('nbc_prepare','nbc_lease_window','nbc_run'))
 last=max(addresses[x] for x in ('nbc_prepare','nbc_lease_window','nbc_run'))
 ends=[addresses[x] for x in ('nnr_plan','nbc_caps_decode','nni_basic') if addresses[x]>last]
 if not ends:raise ValueError('unexpected BASIC TU link order; inspect symbols before bounded adaptation')
 end=min(ends)
 if not 0<end-first<0x10000:raise ValueError('new BASIC TU exceeds targeted64KiB bound')
 helper=dump('--start-address='+hex(first),'--stop-address='+hex(end))
 save('linked-basic-helper-disassembly.txt',helper)
 selected=[(v,t,n) for v,t,n in symbols if first<=v<end or n in {'_'+x for x in (*MANDATORY,*OPTIONAL,'nbc_native','ncd_native','nwfn_native','nfp_native','ncd_io','ncd_bus')}]
 save('linked-basic-symbols.txt',''.join(f'{v:016x} {t} {n}\n' for v,t,n in selected))
 if struct.unpack_from('<I',data)[0]!=0xfeedfacf:raise ValueError('expected64bit MachO')
 segments=[];pos=32
 for _ in range(struct.unpack_from('<I',data,16)[0]):
  cmd,size=struct.unpack_from('<II',data,pos)
  if size<8 or pos+size>len(data):raise ValueError('load command extent')
  if cmd==0x19:
   vm,vs,off,fs=struct.unpack_from('<QQQQ',data,pos+24);segments.append((vm,off,fs))
  pos+=size
 def read(at,n):
  for vm,off,sz in segments:
   if vm<=at and at+n<=vm+sz:return data[off+at-vm:off+at-vm+n]
  raise ValueError('unmapped VM read '+hex(at))
 def va(off):
  for vm,fo,sz in segments:
   if fo<=off<fo+sz:return vm+off-fo
  return None
 bus_address=find('ncd_bus');io_address=find('ncd_io')
 bus=struct.unpack('<7Q',read(bus_address,56));io=struct.unpack('<2Q',read(io_address,16))
 expected=tuple(find('ncd_native_'+x) for x in ('owner','read','write','mread','mwrite','sync','delay'))
 if bus!=expected or io!=(bus_address,find('ncd_native_now')):raise ValueError('actual IO function table mismatch')
 markers={}
 for marker in plan['required_kernel_markers']:
  b=marker.encode();offset=data.find(b)
  if offset<0:raise ValueError('missing marker '+marker)
  address=va(offset);markers[marker]={'file_offset':hex(offset),'vm_address':hex(address) if address is not None else None}
 evidence={'source_commit':args.source_commit,'ci_run':args.ci_run,'kernel':args.kernel_path,'kernel_bytes':len(data),'kernel_sha256':digest,'source_pins':plan['source_pins'],'functions':{n:hex(v) for n,v in addresses.items()},'optional_inlined_or_address':{n:hex(v) if v is not None else 'inlined/no separate symbol' for n,v in optional.items()},'state_symbols':{n:hex(find(n)) for n in ('nbc_native','ncd_native','nwfn_native','nfp_native')},'io_address':hex(io_address),'io_pointers':list(map(hex,io)),'bus_address':hex(bus_address),'bus_pointers':list(map(hex,bus)),'new_basic_tu_range':[hex(first),hex(end)],'markers':markers,'verdict':'EVIDENCE_ONLY_MANUAL_INSTRUCTION_REVIEW_REQUIRED'}
 save('linked-basic-data.json',json.dumps(evidence,indent=2)+'\n')
 print('Bounded extraction complete; manual linked instruction/offset review still required.')
if __name__=='__main__':main()
