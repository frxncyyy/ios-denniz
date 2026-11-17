#!/usr/bin/env python3
"""Bounded original-artifact static source checks, never runtime/USB evidence."""
from pathlib import Path
import runpy,struct,re,hashlib,json
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;B=R/'research/nord2/wifi/ios-service-bridge';sha=lambda b:hashlib.sha256(b).hexdigest()
C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
wpath=R/'research/nord2/ipsw/root-images/mounts/OS/root/usr/sbin/wifid';w=wpath.read_bytes();assert struct.unpack_from('<I',w)[0]==0xfeedfacf
n,size=struct.unpack_from('<II',w,16);segments=[];p=32
for _ in range(n):
 cmd,length=struct.unpack_from('<II',w,p)
 if cmd==0x19:
  name,va,vs,fo,fs=struct.unpack_from('<16sQQQQ',w,p+8);segments.append((va,va+fs,fo,name.rstrip(b'\0').decode()))
 p+=length
assert p==32+size
def wread(a,n):
 for lo,hi,off,name in segments:
  if lo<=a and a+n<=hi:return w[off+a-lo:off+a-lo+n]
 raise AssertionError(hex(a))
assert sha(C.read(0x1d3eb9000,344064))=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
plan=[('wifid_DisassociateExt_status_and_retry',B/'initial-query-audit/wifid-disassembly.txt',wread,0x1000533d0,0x100053444),('wifid_zero_body_status_and_retry',B/'initial-query-audit/wifid-disassembly.txt',wread,0x10014c9ac,0x10014ca10),('immediate_caller_continues_without_event_ack',B/'initial-query-audit/wifid-disassembly.txt',wread,0x100046fac,0x100046fd8),('framework_Disassociate_exports',B/'IO80211-disassembly.txt',C.read,0x1d3ec1768,0x1d3ec177c),('framework_sync_success_branch',B/'IO80211-disassembly.txt',C.read,0x1d3ecdae0,0x1d3ecdb18),('framework_sync_success_zero',B/'IO80211-disassembly.txt',C.read,0x1d3ecfcd8,0x1d3ecfce0),('framework_dispatch_return',B/'IO80211-disassembly.txt',C.read,0x1d3ed09bc,0x1d3ed09fc)]
records=[];out=[]
for name,path,read,lo,hi in plan:
 rows=[]
 for line in path.read_text().splitlines():
  m=re.match(r'^([0-9a-f]+): ((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s',line)
  if m and lo<=int(m[1],16)<hi:assert read(int(m[1],16),4)==bytes.fromhex(m[2]);rows.append(line)
 assert len(rows)==(hi-lo)//4,(name,len(rows));out+=['\n'+name]+rows
 records.append(dict(name=name,start=hex(lo),end_exclusive=hex(hi),instructions=len(rows),original_bytes_sha256=sha(read(lo,hi-lo))))
(D/'verified-disassembly.txt').write_text('\n'.join(out)+'\n')
f=R/'research/nord2/wifi-ios-identity/ios/hardware-review/attempt-final.log';log=f.read_bytes();assert len(log)==427075 and sha(log)=='ec9b1e58c11cf9300e5209b247eb967328a896c11a508ec22d975fe303c9df13'
controls=[dict(request=int(a),bytes=int(b),status=c,t_ms=int(t))for a,b,c,t in re.findall(rb'JOINCONTROL: request=(\d+) bytes=(\d+) status=(\S+) t_ms=(\d+)',log)]
for row in controls:row['status']=row['status'].decode()
pairs=[]
for i,c in enumerate(controls):
 if c['request']==22 and i+1<len(controls)and controls[i+1]['request']==20:pairs.append(dict(disconnect=c,join=controls[i+1],gap_ms=controls[i+1]['t_ms']-c['t_ms']))
summary=dict(status='BOUNDED_STATIC_AUDIT_COMPLETE',runtime_rea_or_device_execution=False,original_wifid=dict(path=str(wpath.relative_to(R)),bytes=len(w),sha256=sha(w)),framework_TEXT_sha256='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea',instruction_ranges=records,total_original_instructions=sum(x['instructions']for x in records),log=dict(path=str(f.relative_to(R)),bytes=len(log),sha256=sha(log)),paired_controls=pairs,status_retry_codes=[-3905,61,16],sleep_per_retry_us=500000,max_sleeps=6,max_explicit_retry_sleep_us=3000000,no_source_proven_event_ack_wait_after_success_in_examined_path=True,no_new_runtime_fix_justified=True)
(D/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps({'instructions':summary['total_original_instructions'],'pairs':len(pairs),'gaps_ms':[p['gap_ms']for p in pairs],'wifid_sha256':sha(w)}))
