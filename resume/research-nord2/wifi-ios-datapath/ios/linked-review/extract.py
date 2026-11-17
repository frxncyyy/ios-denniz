#!/usr/bin/env python3
"""Prepare by default. Exact successful root-supplied artifact pins required to extract."""
from pathlib import Path
import argparse,hashlib,json,re,runpy
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent
SOURCES=['wifi_station_tx.c','wifi_station_tx.h','wifi_main_initializer.c','wifi_main_initializer.h','wifi_main_wfdma_bind.inc','wifi_station_transport.c','wifi_station_transport.h','wifi_station_transport_bind.inc','wifi_station_transaction.c','wifi_station_transaction.h','wifi_wpa2.c','wifi_wpa2.h','wifi_wpa_owner.c','wifi_wpa_nst.c','wifi_station_service_bind.inc','wifi_scan_return_bind.inc','wifi_ios_policy.c','wifi_ios_snapshot.c','wifi_ios_snapshot.h','wifi_scan_plan.c','wifi_scan_service.h','wifi_scan_service_bind.inc','wifi_station_data_diag.c','wifi_station_data_diag.h']
FUNCTIONS=[('normal_tx_builder','_nstx_build'),('txfree_decoder','_nstx_decode_report'),('txfree_ledger','_nstx_report'),('native_step','_nst_step'),('native_replay_replace','_nst_replay_replace_group'),('engine_receive','_nwp_receive'),('engine_group_ready','_nwp_group_ready'),('engine_local_submission','_nwp_security_submitted'),('transaction_eapol','_stc_eapol'),('transaction_receipt','_stc_receipt'),('owner_pump','_nwo_pump'),('connector_event','_nwn_event'),('scan_admission','_niwi_scan_admit'),('scan_bss_filter','_niwa_snapshot_bss_filter'),('data_txs_fields','_nsdd_event'),('data_summary','_nsdd_summary'),('retained_worker','_nss_worker_continue')]
SCOPE=['Normal TXD32/TXP32/payload build, exact HW0 publication path and both DDONE+matching token before page retirement','RXtype6 bounded versions/count/token ledger and separate command/normal-ring accounting, no airACK fabrication','Group1 exact info/MIC/replay/AESunwrap/KDE and duplicate/current-key no reinstall; native per-index WTBL/replay rotation, real G2 submission ordering','Scan BSS2/ANY/FAST and110ms->108TU admission with unchanged allowlists/DFS; prepublication infrastructure row filtering','Bounded advancedTXS/DHCP correlation observations remain diagnostic; no admission/lease/IP proof','Parent handles full generated-source/whole-ISA audit; this checks changed linked bodies/calls only, no hardware claim']
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('--artifact-ready',action='store_true');a.add_argument('--ci',type=int);a.add_argument('--source');a.add_argument('--kernel-sha');a.add_argument('--kernel-bytes',type=int);z=a.parse_args()
if not z.ci:
 assert not z.artifact_ready
 q={'status':'PREPARED_ONLY_NO_ARTIFACT_READ_OR_VERDICT','requires':['Root ARTIFACTS_READY for exact successful CI/attempt/source','Exact root kernel SHA256/length','Matching completed/success run.json','Root source equality/generic ISA audit; do not duplicate watcher/download'],'canonical_source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES},'compiled_report_sha256':sha(P.parent/'compiled-review/attempt3/report.json'),'functions':FUNCTIONS,'direct_callee_bodies':['nst_step→transmit','nst_step→receive','receive→decode'],'scope':SCOPE}
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
# Static helpers may inline; retain direct-callee bytes where a real call exists.
for parent,label,name in [('native_step','native_transmit','transmit'),('native_step','native_receive','receive'),('native_receive','native_decode','decode'),('native_transmit','normal_data_transmit','data_transmit'),('engine_receive','group_receive','group_receive')]:
 path=P/(parent+'.s')
 if not path.exists():continue
 hits=re.findall(r'b(?:l)?\s+(0x[0-9a-f]+) <_'+name+r'>',path.read_text())
 if not hits:continue
 assert len(set(hits))==1,(parent,name,hits)
 start=int(hits[0],16);end=min(s[0] for s in m.syms if s[0]>start and m.text(s[0]));save(label,start,end,'_'+name)
(P/'extraction.json').write_text(json.dumps({'status':'EXTRACTED_PENDING_BOUNDED_MANUAL_REVIEW','ci':z.ci,'source':z.source,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'run_sha256':sha(CI/'run.json'),'functions':records,'canonical_source_pins':q['canonical_source_pins'],'scope':SCOPE},indent=2)+'\n')
