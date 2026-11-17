#!/usr/bin/env python3
"""Bounded static evidence only; no REA engine or runtime execution."""
from pathlib import Path
import runpy,hashlib,json,re,struct,collections
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;B=R/'research/nord2/wifi/ios-service-bridge'
C=runpy.run_path(str(B/'cache_view.py'))['Cache'](R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld')
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(C.read(0x1d3eb9000,344064))=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
ranges=[('exports',0x1d3ec1768,0x1d3ec1804),('dispatch',0x1d3ecdaa4,0x1d3ecdaec),('request22',0x1d3ece8e0,0x1d3ece92c),('carrier_copy_and_call',0x1d3ec3850,0x1d3ec39a0)]
lines=(B/'IO80211-disassembly.txt').read_text().splitlines();out=[];report=[]
for label,lo,hi in ranges:
 rows=[]
 for line in lines:
  m=re.match(r'^([0-9a-f]+): ((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s',line)
  if m and lo<=int(m[1],16)<hi:
   address=int(m[1],16);assert C.read(address,4)==bytes.fromhex(m[2]);rows.append(line)
 assert len(rows)==(hi-lo)//4,(label,len(rows));out+=['\n'+label]+rows
 report.append({'name':label,'start':hex(lo),'end_exclusive':hex(hi),'instructions':len(rows),'original_bytes_sha256':sha(C.read(lo,hi-lo))})
entry=struct.unpack('<H',C.read(0x1d3ef589c+21*2,2))[0];assert entry==902 and 0x1d3ecdac8+entry*4==0x1d3ece8e0
raw=C.read(0x1dabad8b8,32);ptr=struct.unpack_from('<Q',raw,16)[0];length=struct.unpack_from('<Q',raw,24)[0];s=C.read(ptr&0xfffffffff,length);assert s==b'DISASSOC_REASON'
(D/'producer-disassembly.txt').write_text('\n'.join(out)+'\n')
log=(D/'snapshot.log').read_text(errors='replace');controls=[{'request':int(a),'bytes':int(b),'status':c,'t_ms':int(t)}for a,b,c,t in re.findall(r'JOINCONTROL: request=(\d+) bytes=(\d+) status=(\S+) t_ms=(\d+)',log)]
join=[{'reason':int(a),'mode':int(b),'auth_lower':int(c),'auth_upper':int(d),'rsn_bytes':int(e),'assoc_flags':f}for a,b,c,d,e,f in re.findall(r'JOINREQ: bytes=592 reason=(\d+) mode=(\d+) auth_lower=(\d+) auth_upper=(\d+) rsn_bytes=(\d+)[^\n]*?assoc_flags=(\S+)',log)]
pairs=[]
for i,a in enumerate(controls):
 if a['request']==22 and i+1<len(controls)and controls[i+1]['request']==20:pairs.append({'disconnect':a,'association':controls[i+1],'gap_ms':controls[i+1]['t_ms']-a['t_ms']})
summary={'static_evidence_only':True,'framework_TEXT_sha256':'c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea','ranges':report,'jump_entry':{'request':22,'index':21,'value':entry,'target':'0x1d3ece8e0'},'CFString':{'address':'0x1dabad8b8','raw_sha256':sha(raw),'encoded_pointer':hex(ptr),'selected_low36_target':hex(ptr&0xfffffffff),'length':length,'value':s.decode()},'snapshot':json.loads((D/'snapshot.json').read_text()),'controls':controls,'pairs':pairs,'join_numeric_partial_rows':join,'station_rows':log.count('NORD2 WIFI IOS STATION:'),'other_unsupported_get210_16':len(re.findall(r'REQUEST: request=210 bytes=16 unsupported=1',log)),'limits':['Actual device eight-byte payload was not logged: its opaque prefix and reason value remain unknown.','JOINREQ printing interleaves with JOINCONTROL; numeric extraction is diagnostic, not a complete strict receipt.','Selected producer proof is static original-cache evidence, not execution of Apple code.','Unsupported GET210/16 observed, no proven causal gate before the actual later SET20.']}
(D/'evidence.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({'verified_instructions':sum(x['instructions']for x in report),'pairs':len(pairs),'station_rows':summary['station_rows'],'snapshot_sha256':summary['snapshot']['sha256']}))
