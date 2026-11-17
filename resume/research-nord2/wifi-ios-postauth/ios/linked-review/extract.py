#!/usr/bin/env python3
"""Prepare by default. Exact successful root-supplied artifact pins required to extract."""
from pathlib import Path
import argparse,hashlib,json,re,runpy
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent
SOURCES=['wifi_station_transport.c','wifi_station_transport.h','wifi_station_data_diag.c','wifi_station_data_diag.h','wifi_station_transaction.c','wifi_wpa2.c','wifi_wpa2.h','wifi_wpa_diag.c','wifi_wpa_diag.h','wifi_scan_return_bind.inc','wifi_station_service_bind.inc']
FUNCTIONS=[('transaction_eapol','_stc_eapol'),('wpa_header_budget','_nwpd_take'),('wpa_header_print','_nwpd_observe'),('preengine_numeric_gate','_nwpd_gate_record'),('preengine_gate_print','_nwpd_gate_observe'),('data_classifier','_nsdd_classify'),('txs_abi_decoder','_nsdd_decode_txs'),('data_publish','_nsdd_publish'),('data_receive','_nsdd_receive'),('txs_budget_print','_nsdd_event'),('data_summary','_nsdd_summary'),('native_step','_nst_step'),('retained_worker','_nss_worker_continue')]
SCOPE=['Postauthorized scan return removes historical joined-only refusal; terminal/cookie/key/port/physical/sealed-origin gates retained in inlined retained worker','Actual transaction pre-engine gate call before unchanged stop; no payload pointer to numeric helper','WPAHDR failure/header hook after real engine evaluation, independent finite early/late/failure budgets and single-line print','Data/TXS classify and numeric hooks only; source88B ABI/advanced-bit, shared host PID ambiguity, native RX after rearm rather than BSD delivery','Bounded transmit/receive/decode direct-callee bodies: no changed acceptance/descriptor/receipt policy; formatter hooks use owned copies','No full WiFi/crypto re-review; parent generated-source/ISA audit remains separate; no hardware/IP/rekey success proof']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('--artifact-ready',action='store_true');a.add_argument('--ci',type=int);a.add_argument('--source');a.add_argument('--kernel-sha');a.add_argument('--kernel-bytes',type=int);z=a.parse_args()
if not z.ci:
 assert not z.artifact_ready
 q={'status':'PREPARED_ONLY_NO_ARTIFACT_READ_OR_VERDICT','requires':['Root ARTIFACTS_READY for exact successful CI/attempt/source','Exact root kernel SHA256/length','Matching completed/success run.json','Root source equality/generic ISA audit; do not duplicate watcher/download'],'canonical_source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES},'compiled_report_sha256':sha(P.parent/'compiled-review/attempt1/report.json'),'functions':FUNCTIONS,'direct_callee_bodies':['nst_step→transmit','nst_step→receive','receive→decode'],'scope':SCOPE}
 (P/'requirements.json').write_text(json.dumps(q,indent=2)+'\n');print('PREPARED ONLY; no CI artifact read');raise SystemExit(0)
assert z.artifact_ready and z.source and z.kernel_sha and z.kernel_bytes
q=json.loads((P/'requirements.json').read_text());assert all(sha(R/'shim/nord2-xnu'/k)==v for k,v in q['canonical_source_pins'].items())
CI=R/f'research/ci-{z.ci}';run=json.loads((CI/'run.json').read_text());assert run['status']=='completed' and run['conclusion']=='success' and run['headSha']==z.source
k=CI/'xnu-kernel/kernel.development.sweet';assert k.stat().st_size==z.kernel_bytes and sha(k)==z.kernel_sha
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k);records=[]
def save(label,start,end,symbol):
 body=m.read(start,end-start);(P/(label+'.bin')).write_bytes(body);(P/(label+'.s')).write_text(m.dump(start,end));records.append({'label':label,'symbol':symbol,'start':hex(start),'end':hex(end),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()})
for label,name in FUNCTIONS:
 start,end,symbol=m.bounds(name,True);save(label,start,end,symbol)
for parent,label,name in [('native_step','native_transmit','transmit'),('native_step','native_receive','receive'),('native_receive','native_decode','decode')]:
 text=(P/(parent+'.s')).read_text();hits=re.findall(r'bl\s+(0x[0-9a-f]+) <_'+name+r'>',text);assert len(set(hits))==1,(parent,name,hits)
 start=int(hits[0],16);end=min(s[0] for s in m.syms if s[0]>start and m.text(s[0]));save(label,start,end,'_'+name)
(P/'extraction.json').write_text(json.dumps({'status':'EXTRACTED_PENDING_BOUNDED_MANUAL_REVIEW','ci':z.ci,'source':z.source,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'run_sha256':sha(CI/'run.json'),'functions':records,'canonical_source_pins':q['canonical_source_pins'],'scope':SCOPE},indent=2)+'\n')
