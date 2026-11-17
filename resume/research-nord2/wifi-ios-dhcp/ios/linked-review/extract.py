#!/usr/bin/env python3
"""Bounded DHCP delta extraction. Default prepares only; no gates or devices.
Actual extraction requires parent CI_READY identity and verified existing kernel.
"""
from pathlib import Path
import argparse,hashlib,json,re,runpy
P=Path(__file__).resolve().parent
R=next(p for p in P.parents if (p/'AGENTS.md').is_file())
SOURCES=['wifi_station_wire.c','wifi_station_wire.h','wifi_station_source_layout.h','wifi_station_transport.c','wifi_station_data_diag.c','wifi_station_data_diag.h']
FUNCTIONS=[('sta_builder','_sw_sta'),('peer_progress','_peer_progress'),('native_receive','_receive'),('native_decode','_decode'),('rx_error_take','_nsdd_rx_error_take'),('rx_error_emit','_nsdd_rx_error')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def save(name,data):(P/name).write_text(json.dumps(data,indent=2)+'\n')
a=argparse.ArgumentParser();a.add_argument('--artifact-ready',action='store_true');a.add_argument('--ci',type=int);a.add_argument('--attempt',type=int);a.add_argument('--source');a.add_argument('--kernel-sha');a.add_argument('--kernel-bytes',type=int);v=a.parse_args()
if not v.artifact_ready:
 assert not any((v.ci,v.attempt,v.source,v.kernel_sha,v.kernel_bytes)), 'No artifact identity is accepted in prepare mode'
 report=P.parent/'compiled-review/attempt1/report.json';pins=P.parent/'compiled-review/attempt1/source-pins.json';c=json.loads(report.read_text());q=json.loads(pins.read_text());assert c['passed'] and len(c['commands'])==16 and all(x['exit']==0 for x in c['commands']);assert c['source_pins_sha256']==sha(pins)
 assert q['patch_sha256']=='71c77cfc866541516d21b86530a15e223343edf421174486aa2e555f83055831'==sha(R/'xnu/patches/0082-nord2-early-probe.patch')
 actual={f:sha(R/'shim/nord2-xnu'/f) for f in SOURCES}
 assert all(q['canonical_siblings']['shim/nord2-xnu/'+f]==h for f,h in actual.items())
 focused={}
 for name in ('wifi_station_wire','wifi_station_transport','wifi_station_data_diag'):
  x=next(x for x in c['commands'] if x['name']==name);assert '-mcpu=generic' in x['argv'] and '-march=armv8.2-a+crypto' in x['argv'];o=report.parent/(name+'.o');assert sha(o)==x['object_sha256'];focused[name]={'path':str(o.relative_to(R)),'sha256':sha(o),'generated_source_sha256':x['source_sha256']}
 save('requirements.json',{'status':'PREPARED_ONLY_WAIT_CI_READY','verdict':None,'ci':None,'source':None,'kernel_sha256':None,'patch_sha256':q['patch_sha256'],'canonical_source_pins':actual,'compiled_report':str(report.relative_to(R)),'compiled_report_sha256':sha(report),'compiled_source_pins_sha256':sha(pins),'focused_objects':focused,'functions':FUNCTIONS,'scope':['STA input requires reserved255, peerWLAN remains separate, exact byte55 store','Actual peer_progress passesSTA255 while separateBSS keepsactualgroup mapping; state1/state3 and activation/cleanup recipe unchanged','Existing receive error-gate masks0x0f800000/0x0b800000/0x1803 call diagnostic then unchangedreject/return; no admission widening','RX detail fixedheader bounds24..3840/declaredextent/type2orSWframe, nonzeroerrors,8recordindependentbudget, no payloadreads','Exact RXERROR numeric-only literal and emitter call; no key/address/packet attribution or air/lease claims'],'not_repeated':['Unchanged27body inventory','Whole kernel source/ISA audit (parent owned)','Unchanged WPA/GTK/key/DMA/scan behavior'],'automatic_object_match':None,'human_semantic_linked_review_pending':True,'gates_changed':False})
 print('PREPARED ONLY: six bounded bodies, awaiting parent CI_READY');raise SystemExit(0)
assert v.ci and v.attempt and re.fullmatch('[0-9a-f]{40}',v.source or '') and re.fullmatch('[0-9a-f]{64}',v.kernel_sha or '') and v.kernel_bytes
q=json.loads((P/'requirements.json').read_text());assert all(sha(R/'shim/nord2-xnu'/f)==h for f,h in q['canonical_source_pins'].items());assert sha(R/'xnu/patches/0082-nord2-early-probe.patch')==q['patch_sha256']
CI=R/f'research/ci-{v.ci}';meta=json.loads((CI/'run.json').read_text());assert (meta['databaseId'],meta['attempt'],meta['headSha'],meta['status'],meta['conclusion'])==(v.ci,v.attempt,v.source,'completed','success');k=CI/'xnu-kernel/kernel.development.sweet';assert k.stat().st_size==v.kernel_bytes and sha(k)==v.kernel_sha
assert [x.split()[0] for x in (k.parent/'SHA256SUMS').read_text().splitlines() if len(x.split())==2 and Path(x.split()[1].lstrip('*')).name==k.name]==[v.kernel_sha]
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k);out=P/'artifact';out.mkdir(exist_ok=False);records=[]
for label,symbol in FUNCTIONS:
 hits=[(a,n)for a,_,n in m.syms if m.text(a)and n==symbol]
 if not hits:
  assert label in ('peer_progress','native_receive','native_decode'),(symbol,'required external helper missing')
  records.append({'label':label,'symbol':symbol,'status':'NO_SEPARATE_BODY_REVIEW_INLINED_CALLER'});continue
 start,end,sym=m.bounds(symbol,True);b=m.read(start,end-start);(out/(label+'.bin')).write_bytes(b);(out/(label+'.s')).write_text(m.dump(start,end));records.append({'label':label,'symbol':sym,'start':hex(start),'end':hex(end),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
# Only if compiler inlines a selected static body, retain its actual caller.
if any(x.get('status') for x in records):
 start,end,sym=m.bounds('_nst_step',True);b=m.read(start,end-start);(out/'caller_step.bin').write_bytes(b);(out/'caller_step.s').write_text(m.dump(start,end));records.append({'label':'caller_step','symbol':sym,'start':hex(start),'end':hex(end),'bytes':len(b),'sha256':hashlib.sha256(b).hexdigest()})
source=(R/'shim/nord2-xnu/wifi_station_data_diag.c').read_text();formats=re.findall(r'printf\("(NORD2 WIFI RXERROR: [^"]*)"',source);assert len(formats)==1;literal=formats[0].replace('\\n','\n');assert '%s'not in literal and '%p'not in literal;needle=literal.encode()+b'\0';assert needle in m.data
save('extraction.json',{'status':'EXTRACTED_PENDING_BOUNDED_HUMAN_REVIEW','ci':v.ci,'attempt':v.attempt,'source':v.source,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'run_json_sha256':sha(CI/'run.json'),'functions':records,'canonical_source_pins':q['canonical_source_pins'],'compiled_report_sha256':q['compiled_report_sha256'],'emitter':{'literal':literal,'sha256':hashlib.sha256(needle).hexdigest(),'kernel_file_offset':hex(m.data.index(needle))},'scope':q['scope'],'automatic_object_match':None,'human_semantic_linked_review_pending':True,'gates_changed':False});print('EXTRACTED bounded DHCP delta; manual linked verdict still pending')
