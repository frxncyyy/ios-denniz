#!/usr/bin/env python3
"""Prepare only by default; artifact extraction requires root's exact successful pins."""
from pathlib import Path
import argparse,json,hashlib,runpy
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent
SOURCES=['wifi_station_transport.c','wifi_station_transport.h','wifi_station_transaction.c','wifi_wpa2.c','wifi_wpa2.h','wifi_wpa_owner.c','wifi_wpa_owner.h','wifi_wpa_nst.c','wifi_station_service_bind.inc','wifi_scan_return_bind.inc','wifi_scan_service_bind.inc','wifi_normal_transport.c','wifi_normal_transport.h','wifi_normal_transport_bind.inc']
FUNCTIONS=[('native_step','_nst_step',True),('transaction_receipt','_stc_receipt',True),('security_submitted','_nwp_security_submitted',True),('owner_receipt','_nwo_complete',True),('eapol_connector','_nwn_event',True),('authorize','_nst_authorize',True),('return_prepare','_nct_scan_return_prepare',True),('continued_origin','_nct_continue_prepare',True),('retained_worker','_nss_worker_continue',True)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('--ci',type=int);a.add_argument('--source');a.add_argument('--kernel-sha');a.add_argument('--kernel-bytes',type=int);z=a.parse_args()
if not z.ci:
 (P/'requirements.json').write_text(json.dumps({'status':'PREPARED_NOT_EXECUTED_NO_ARTIFACT_VERDICT','requires':['Root ARTIFACTS_READY notice','Exact CI/source/kernel SHA256 and length','Successful matching run.json','Root generated-source equality audit; generic ISA delegated to root'],'source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES},'compiled_review_manifest_sha256':sha(P.parent/'compiled-review/attempt1/report.json'),'functions':FUNCTIONS,'manual_scope':['Verified-DDONE typed SECURITY_SUBMITTED15 / DATA_SUBMITTED16; no AIR acknowledgement fabrication','Owner expected receipt8 and M4 local receipt to actual PTK/GTK authorization barriers','Host-only PID127 versus consumed management1..126; late optional TXS isolation','SCANRETURN terminal cleanup/callback closure; sealed persistent origin and all-five-RX continuation','Static inlined transmit/return guards inspected through direct calls or bounded relevant worker excerpts only; unchanged driver excluded']},indent=2)+'\n');print('PREPARED; no artifact inspected');raise SystemExit(0)
assert z.source and z.kernel_sha and z.kernel_bytes
CI=R/f'research/ci-{z.ci}';run=json.loads((CI/'run.json').read_text());assert run['status']=='completed' and run['conclusion']=='success' and run['headSha']==z.source
k=CI/'xnu-kernel/kernel.development.sweet';assert k.stat().st_size==z.kernel_bytes and sha(k)==z.kernel_sha
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k);records=[]
for label,pattern,exact in FUNCTIONS:
 start,end,symbol=m.bounds(pattern,exact);body=m.read(start,end-start);(P/(label+'.bin')).write_bytes(body);(P/(label+'.s')).write_text(m.dump(start,end));records.append({'label':label,'symbol':symbol,'start':hex(start),'end':hex(end),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()})
(P/'extraction.json').write_text(json.dumps({'status':'EXTRACTED_PENDING_MANUAL_REVIEW','ci':z.ci,'source':z.source,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'functions':records,'canonical_source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES}},indent=2)+'\n')
