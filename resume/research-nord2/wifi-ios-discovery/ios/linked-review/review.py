#!/usr/bin/env python3
"""Bounded discovery-delta extraction. Never writes a linked PASS or any gate.
No arguments prepare an unbound source/object inventory. Root-supplied source/CI
pins are required to bind it; successful matching artifact metadata is required
before extracting the nine selected linked functions. No build/poll/download.
"""
from pathlib import Path
import argparse,hashlib,json,re,runpy,subprocess
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'shim/nord2-xnu').is_dir())
T=R.parent/'toolchains/proton-clang/bin'
PREVIOUS=R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'
# Reuse only the bounded Mach-O reader, not old prepare/extract/report state.
MachO=runpy.run_path(str(PREVIOUS))['MachO']
FUNCTIONS=[
 ('policy','_niwi_scan_admit',True,'wifi_ios_policy'),
 ('bss_metadata','_niwb_bss_metadata',True,'wifi_ios_bss'),
 ('snapshot','_niwa_snapshot_prepare',True,'wifi_ios_snapshot'),
 ('backend_decode','_nsb_decode',True,'wifi_scan_bss'),
 ('backend_add','_nsb_add',True,'wifi_scan_bss'),
 ('client_scan','NiwlLifecycleClient::scanLocked(',False,'wifi_ios_client'),
 ('client_completed','NiwlLifecycleClient::completed(',False,'wifi_ios_client'),
 ('client_allocate','NiwlLifecycleClient::initWithTask(',False,'wifi_ios_client'),
 ('client_free','NiwlLifecycleClient::free(',False,'wifi_ios_client')]
SOURCES=['shim/nord2-xnu/'+n for n in (
 'wifi_ios_policy.c','wifi_ios_policy.h','wifi_ios_snapshot.c','wifi_ios_snapshot.h',
 'wifi_ios_bss.c','wifi_ios_bss.h','wifi_scan_bss.c','wifi_scan_bss.h','wifi_ios_client.cpp',
 'wifi_ios_scan_contract.h','wifi_ios_cursor.h','wifi_normal_rx.h')]
TYPES=('nsb_record','nsb_scan','niwb_bss','niwa_snapshot')
EXPECTED_SIZES={'nsb_record':1112,'nsb_scan':18336,'niwb_bss':1104,'niwa_snapshot':18696}
# Source baseline only: no previous linked verdict is inherited.
BASE_COMMIT='27717fffcbbd0221334a4b18df6565551f877ab3'

def run(*args):return subprocess.check_output(list(map(str,args)),text=True)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,value):
 q=P/name;q.parent.mkdir(parents=True,exist_ok=True);q.write_text(json.dumps(value,indent=2)+'\n')
def schema(path,outdir,label):
 text=run(T/'llvm-dwarfdump',*['--name='+n for n in TYPES],
          '--show-children','--recurse-depth=1',path)
 assert len(text)<2000000
 (outdir/(label+'-schema.txt')).write_text(text)
 found={}
 for block in re.split(r'(?=^0x[0-9a-f]+: DW_TAG_structure_type)',text,flags=re.M):
  n=re.search(r'DW_AT_name\s+\("([^"\n]+)"\)',block)
  z=re.search(r'DW_AT_byte_size\s+\((0x[0-9a-f]+|\d+)\)',block)
  if not n or not z or n[1] not in TYPES:continue
  members={}
  for b in re.split(r'(?=^0x[0-9a-f]+:\s+DW_TAG_member)',block,flags=re.M)[1:]:
   mn=re.search(r'DW_AT_name\s+\("([^"\n]+)"\)',b)
   at=re.search(r'DW_AT_data_member_location\s+\((0x[0-9a-f]+|\d+)\)',b)
   if mn and at:members[mn[1]]=int(at[1],0)
  row={'bytes':int(z[1],0),'members':members}
  if n[1] in found:assert found[n[1]]==row
  found[n[1]]=row
  assert row['bytes']==EXPECTED_SIZES[n[1]],(label,n[1],row)
 return {'path':str(path),'sha256':sha(path),'types':found,
         'dwarf_sha256':sha(outdir/(label+'-schema.txt'))}

def prepare(args):
 assert bool(args.source_commit)==bool(args.ci_run),'supply both source and CI pins'
 source={s:sha(R/s) for s in SOURCES}
 head=run('git','-C',R,'rev-parse','HEAD').strip()
 commit=None
 if args.source_commit:
  commit=run('git','-C',R,'rev-parse',args.source_commit+'^{commit}').strip()
  for s,h in source.items():assert hashlib.sha256(subprocess.check_output(
   ['git','-C',str(R),'show',commit+':'+s])).hexdigest()==h,s
  delta=run('git','-C',R,'diff',BASE_COMMIT,commit,'--',*SOURCES)
 else:delta=run('git','-C',R,'diff','HEAD','--',*SOURCES)
 (P/'source-delta.patch').write_text(delta)
 records=json.loads((P.parent/'focused/commands.json').read_text())
 out=P/'focused';out.mkdir(exist_ok=True);objects={};schemas={}
 required={x[3] for x in FUNCTIONS}
 for record in records:
  unit=record['name'];obj=P.parent/'focused'/(unit+'.o')
  if record['exit'] or not obj.is_file():continue
  generated=Path(next(s for s in record['argv'] if s.endswith('/'+unit+('.cpp' if unit=='wifi_ios_client' else '.c'))))
  assert sha(generated)==record['source_sha256']
  assert '-mcpu=generic' in record['argv'] and '-march=armv8.2-a+crypto' in record['argv']
  if unit in required:
   canonical=R/'shim/nord2-xnu'/generated.name
   assert generated.read_bytes()==b'#include <arm64/sweet_early.h>\n#if defined(NORD2)\n'+canonical.read_bytes()+b'\n#endif /* NORD2 */\n'
   buildroot=next(a for a in generated.parents if (a/'bsd/dev').is_dir())
   for h in ('wifi_scan_bss.h','wifi_ios_snapshot.h','wifi_ios_bss.h','wifi_ios_cursor.h','wifi_ios_scan_contract.h'):
    assert (buildroot/'bsd/dev'/h).read_bytes()==(R/'shim/nord2-xnu'/h).read_bytes(),(unit,h)
   objects[unit]={'path':str(obj),'sha256':sha(obj),'source_sha256':record['source_sha256']}
  # Types used across backend/client objects; absent debug types are recorded,
  # never treated as a successful schema check for that TU.
  schemas[unit]=schema(obj,out,unit)
 assert set(objects)==required,(required,set(objects))
 boundaries={}
 cache={}
 for label,pattern,exact,unit in FUNCTIONS:
  obj=cache.setdefault(unit,MachO(Path(objects[unit]['path'])))
  a,end,name=obj.bounds(pattern,exact)
  boundaries[label]={'symbol':name,'bytes':end-a,'object':unit}
 # Core expected schema must be present in the producer/consumer objects.
 for unit,types in [('wifi_ios_snapshot',TYPES),('wifi_scan_bss',('nsb_record','nsb_scan')),
                    ('wifi_ios_client',('niwa_snapshot',))]:
  assert all(t in schemas[unit]['types'] for t in types),(unit,types)
 result={'status':'PENDING_ARTIFACT' if commit else 'PENDING_ROOT_SOURCE_CI_PINS',
  'linked_verdict':None,'source_baseline':BASE_COMMIT,'observed_worktree_head':head,'expected_source':commit,'expected_ci_run':args.ci_run,
  'sources':source,'focused_objects':objects,'focused_schema':schemas,'focused_function_boundaries':boundaries,
  'reader_dependency':{'path':str(PREVIOUS),'sha256':sha(PREVIOUS),'reuse':'MachO only; no previous verdict or gates'},
  'not_repeated':['registry/owner/DMA baseline','unchanged event/result callgraph','full-kernel disassembly/ISA scan','quiesce (separate reviewer)'],
  'expected_sizes_only_until_linked_review':EXPECTED_SIZES}
 save('pending.json',result);return result

def extract(args,pending):
 assert pending['expected_source'] and args.kernel and args.run_json and args.sha256 and args.bytes
 kernel=Path(args.kernel).resolve();rj=Path(args.run_json).resolve();metadata=json.loads(rj.read_text())
 assert metadata['databaseId']==args.ci_run and metadata['headSha']==pending['expected_source']
 assert metadata['conclusion']=='success' and kernel.stat().st_size==args.bytes and sha(kernel)==args.sha256
 linked=MachO(kernel);out=P/'artifact';out.mkdir(exist_ok=True);rows={};total=0;objects={}
 for label,pattern,exact,unit in FUNCTIONS:
  a,end,name=linked.bounds(pattern,exact);total+=end-a;assert total<=65536
  body=linked.read(a,end-a);text=linked.dump(a,end);(out/(label+'.txt')).write_text(text)
  meta=pending['focused_objects'][unit];op=Path(meta['path']);assert sha(op)==meta['sha256']
  obj=objects.setdefault(unit,MachO(op));oa,oe,_=obj.bounds(pattern,exact);ob=obj.read(oa,oe-oa)
  (out/(label+'-object.txt')).write_text(obj.dump(oa,oe))
  calls=[]
  for match in re.finditer(r'\bbl\s+(0x[0-9a-f]+)',text):
   target=int(match[1],16);names=sorted({n for va,_,n in linked.syms if va==target})
   calls.append({'target':hex(target),'symbols':names})
  rows[label]={'symbol':name,'address':hex(a),'end':hex(end),'bytes':end-a,
   'code_sha256':hashlib.sha256(body).hexdigest(),'disassembly_sha256':sha(out/(label+'.txt')),
   'object_bytes':len(ob),'exact_object_bytes_equal':body==ob,'direct_calls':calls,
   'comparison_meaning':'match is evidence only; mismatch may be normal compiler/link variation and needs semantic branch review'}
 logs={}
 for label in ('SCANREQ','ADMISSION','CHANNEL','ENQUEUE','COMPLETE','CLIENT'):
  prefix=('NORD2 WIFI IOS '+label+':').encode();hits=[]
  for section,va,z,fo in linked.sections:
   if section!='__cstring':continue
   data=linked.data[fo:fo+z];at=0
   while True:
    at=data.find(prefix,at)
    if at<0:break
    stop=data.find(b'\0',at);assert at<stop<=at+2048
    hits.append({'address':hex(va+at),'string':data[at:stop].decode()});at=stop+1
  assert len(hits)==1,(label,hits);logs[label]=hits[0]
 ci_schema={}
 for spec in args.schema_object:
  unit,path=spec.split('=',1);assert unit in pending['focused_schema'] and unit not in ci_schema
  ci_schema[unit]=schema(Path(path).resolve(),out,'ci-'+unit)
  for typ,layout in ci_schema[unit]['types'].items():
   assert layout==pending['focused_schema'][unit]['types'][typ],(unit,typ)
 result={'status':'EXTRACTED_PENDING_MANUAL_LINKED_REVIEW','linked_verdict':None,
  'identity':{'source_commit':pending['expected_source'],'ci_run':args.ci_run,'kernel':str(kernel),
   'bytes':args.bytes,'sha256':args.sha256,'run_json_sha256':sha(rj)},
  'functions':rows,'total_disassembled_code_bytes':total,'log_strings':logs,
  'ci_object_schema':ci_schema,'schema_limit':'focused expectations only unless matching exact CI object/schema evidence is supplied',
  'sources':pending['sources']}
 save('artifact/extraction.json',result)
 print('EXTRACTED nine bounded functions;',total,'code bytes; linked verdict remains pending manual review')

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--source-commit');p.add_argument('--ci-run',type=int)
 p.add_argument('--kernel');p.add_argument('--run-json');p.add_argument('--sha256');p.add_argument('--bytes',type=int)
 p.add_argument('--schema-object',action='append',default=[],metavar='UNIT=PATH')
 args=p.parse_args();pending=prepare(args)
 if any((args.kernel,args.run_json,args.sha256,args.bytes,args.schema_object)):extract(args,pending)
 else:print(pending['status']+': nine-function plan and focused schema prepared; no linked verdict')
if __name__=='__main__':main()
