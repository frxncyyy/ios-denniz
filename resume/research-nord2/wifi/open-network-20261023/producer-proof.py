#!/usr/bin/env python3
"""Bounded saved-disassembly/original-cache byte proof; not REA/runtime evidence."""
from pathlib import Path
import runpy,re,hashlib,json,argparse
R=next(p for p in Path(__file__).resolve().parents if(p/'AGENTS.md').exists());D=Path(__file__).resolve().parent;B=R/'research/nord2/wifi/ios-service-bridge';a=argparse.ArgumentParser();a.add_argument('--output',type=Path,required=True);x=a.parse_args();x.output.mkdir(parents=True,exist_ok=False)
Cache=runpy.run_path(str(B/'cache_view.py'),run_name='cache_view_open')['Cache'];c=Cache(R/'research/nord2/ipsw/root-images/mounts/SystemOS/root/System/Library/Caches/com.apple.dyld');text=c.read(0x1d3eb9000,344064);assert hashlib.sha256(text).hexdigest()=='c1e1afa676d75de03fd7bee970ccf8cbf63d6199e862a48ebed41aeea0df3aea'
saved=B/'station-wire-candidate/ios-association/associate-builder.txt';ranges=[(0x1d3ebf1ec,0x1d3ebf21c),(0x1d3ebf830,0x1d3ebf888),(0x1d3ebfd54,0x1d3ebfdf8),(0x1d3ebfebc,0x1d3ebfec8)];seen=[]
for line in saved.read_text().splitlines():
 m=re.match(r'^([0-9a-f]+):\s+((?:[0-9a-f]{2} ){3}[0-9a-f]{2})\s+(.*)',line)
 if m and any(lo<=int(m[1],16)<hi for lo,hi in ranges):assert c.read(int(m[1],16),4)==bytes.fromhex(m[2]);seen.append(int(m[1],16))
assert len(seen)==sum((hi-lo)//4 for lo,hi in ranges)
report={'passed':True,'method':'ordinary read-only original split-cache bytes vs existing disassembly; no REA provider or runtime invocation','framework_text_sha256':hashlib.sha256(text).hexdigest(),'saved_disassembly_sha256':hashlib.sha256(saved.read_bytes()).hexdigest(),'instructions_verified':len(seen),'ranges':[[hex(lo),hex(hi)] for lo,hi in ranges],'anchors':{'base_record_zero':'1d3ebf208..218','request_extent20_592':'1d3ebf830..844','mode_default2':'1d3ebf880..884','lower_open1':'1d3ebfd58..5c','key_record_zero56_through199':'1d3ebfdbc..fde8','wpa_only_key_version1':'1d3ebfebc..fec4'},'open_profile':{'record_version':1,'mode':2,'lower_auth':1,'upper_auth':0,'key_version':0,'key_bytes':0,'key_kind':0,'rsn_bytes':0,'native_flags':0},'limit':'Static branch/storage evidence; no complete CFDictionary invocation or hardware OPEN claim. Conditional extraoptions remain refused.'}
(x.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',len(seen),'selected OPEN producer original-cache instruction anchors')
