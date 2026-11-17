#!/usr/bin/env python3
"""Read-only bounded original-cache validation + synthetic codec tests."""
from pathlib import Path
import hashlib,json,re,sys,struct,tempfile,subprocess
P=Path(__file__).resolve().parent;B=P.parent;R=B.parents[3]
sys.path.insert(0,str(B));from cache_view import Cache
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
C=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
e=json.loads((B/'evidence.json').read_text());text=C.read(0x1d3eb9000,344064)
assert hashlib.sha256(text).hexdigest()==e['framework_original_text_sha256']
ranges=[('small_GET_choice',0x1d3ecad2c,0x1d3ecad88),('small_GET_IOUC_call',0x1d3ecaff8,0x1d3ecb078),('small_GET_ioctl_call',0x1d3ecb340,0x1d3ecb35c),('small_SET_choice',0x1d3ec358c,0x1d3ec35d8),('large_GET_choice',0x1d3ed12b0,0x1d3ed12f8),('large_GET_IOUC_call',0x1d3ed16f0,0x1d3ed1760),('large_SET_choice',0x1d3ebc0c8,0x1d3ebc120),('large_SET_IOUC_call',0x1d3ebc49c,0x1d3ebc4cc),('small_SET_IOUC_call',0x1d3ec3974,0x1d3ec39a4),('bind_query43_and_selector1',0x1d3ed4ef0,0x1d3ed5074),('event_selector5',0x1d3ed20dc,0x1d3ed2140),('event_nonzero_alloc',0x1d3ed21a4,0x1d3ed21ec),('event_setup3',0x1d3ed226c,0x1d3ed228c),('event_map_data',0x1d3ed22e4,0x1d3ed2314),('event_zero_failure',0x1d3ed23b8,0x1d3ed23bc),('event_map_state',0x1d3ed23ec,0x1d3ed241c),('event_async2',0x1d3ed24fc,0x1d3ed2558),('selector1_error',0x1d3ed5394,0x1d3ed53ac),('rebind_selector1',0x1d3ee63fc,0x1d3ee6424),('ioctl_stub',0x1d3ef5188,0x1d3ef5194)]
lines=[];instructions={};coverage=[]
all_lines=(B/'IO80211-disassembly.txt').read_text().splitlines()
for name,lo,hi in ranges:
 block=[]
 for line in all_lines:
  m=re.match(r'([0-9a-f]+): ((?:[0-9a-f]{2} ){4})',line)
  if m and lo<=int(m[1],16)<hi:
   a=int(m[1],16);raw=bytes.fromhex(m[2]);assert C.read(a,4)==raw,(name,hex(a));block.append(line);instructions[a]=raw
 assert len(block)==(hi-lo)//4,(name,len(block))
 lines.extend(['\n# '+name]+block);coverage.append({'name':name,'start':hex(lo),'end_exclusive':hex(hi),'bytes':hi-lo,'sha256':hashlib.sha256(C.read(lo,hi-lo)).hexdigest()})
# Actual strings/pointers, not names inferred solely from disassembly comments.
entry=C.read(0x1dabab390,16);rid,rawptr=struct.unpack('<QQ',entry);assert rid==43
name_va=rawptr&0xfffffffff;request_name=C.read(name_va,80).split(b'\0')[0].decode();assert request_name=='APPLE80211_IOC_DRIVER_VERSION'
log=C.read(0x1d3f0996a,100).split(b'\0')[0].decode();assert log=='%{public}s: %@ Sanity test passed. Version - %s'
rawgot=struct.unpack('<Q',C.read(0x1dac9c888,8))[0];dep=json.loads((B/'dependency-exports.json').read_text());assert rawgot&0xfffffffff==dep['_ioctl']
(P/'source-excerpt.txt').write_text('\n'.join(lines)+'\n')
report={'method':'ordinary shell/static original split-cache byte validation; no REA Evidence/provider/runtime','rea_tools_available':False,'rea_cli_available':False,'install_or_configuration_change':False,'framework_original_text_sha256':hashlib.sha256(text).hexdigest(),'original_text_bytes':len(text),'range_bytes_verified':sum(x['bytes'] for x in coverage),'ranges':coverage,'request43':{'name':request_name,'table_va':'0x1dabab390','name_va':hex(name_va),'bind_log_va':'0x1d3f0996a','bind_log':log,'observed_input_bytes':15408,'input_payload_bytes':0,'observed_output_capacity':256,'precleared_output_bytes_including_extra_sentinel':257,'minimum_nonempty_reply_enforced_by_bind':False,'output_used_for_optional_log_only':True},'ioctl_fallback':{'stub':'0x1d3ef5188','GOT':'0x1dac9c888','encoded_pointer':hex(rawgot),'low36_resolved_export':hex(dep['_ioctl']),'decode_limit':'Focused low36 cache-pointer comparison to exact dependency export; no general slide/fixup reconstruction.'},'inputs':{str(p.relative_to(R)):sha(p) for p in [B/'evidence.json',B/'framework-map.json',B/'IO80211-disassembly.txt',B/'dependency-exports.json',B/'cache_view.py',B/'control_contract.c',B/'control_contract.h']}}
(P/'source-evidence.json').write_text(json.dumps(report,indent=2)+'\n')
with tempfile.TemporaryDirectory(prefix='niwb-') as td:
 exe=Path(td)/'test';cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(B),str(P/'binding_contract.c'),str(P/'test.c'),str(B/'control_contract.c'),'-o',str(exe)]
 subprocess.run(cmd,check=True);s=subprocess.check_output([str(exe)],text=True);(P/'test.log').write_text(s);print(s.strip());print('PASS',report['range_bytes_verified'],'original-cache instruction bytes verified')
