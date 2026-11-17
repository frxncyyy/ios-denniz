#!/usr/bin/env python3
"""Prepare only by default; artifact extraction requires root's exact successful pins."""
from pathlib import Path
import argparse,json,hashlib,runpy
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent
SOURCES=['wifi_station_transport.c','wifi_station_transport.h','wifi_station_transaction.c','wifi_station_diag.c','wifi_station_diag.h','wifi_wpa_nst.c','wifi_ios_client.cpp']
FUNCTIONS=[('native_step','_nst_step',True),('transaction_receipt','_stc_receipt',True),('diag_tx','_nstd_tx',True),('diag_txs','_nstd_txs',True),('diag_eapol','_nstd_eapol',True),('diag_rx','_nstd_rx',True),('diag_summary','_nstd_summary',True),('diag_log','_nstd_log',True),('eapol_connector','_nwn_event',True),('client_timing','NiwlLifecycleClient::externalMethod(',False)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=argparse.ArgumentParser();a.add_argument('--ci',type=int);a.add_argument('--source');a.add_argument('--kernel-sha');a.add_argument('--kernel-bytes',type=int);z=a.parse_args()
if not z.ci:
 (P/'requirements.json').write_text(json.dumps({'status':'PREPARED_NOT_EXECUTED_NO_ARTIFACT_VERDICT','requires':['Root ARTIFACTS_READY notice','Exact CI/source/kernel SHA256 and length','Successful matching run.json','Root generated-source equality audit; generic ISA delegated to root'],'source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES},'compiled_review_manifest_sha256':sha(P.parent/'compiled-review/manifest.json'),'functions':FUNCTIONS,'manual_scope':['Known-DDONE-only negative256, genuine queued result final bounded RX pass','DEAUTH-only negative continuation, no deletion/retire success fabrication','Diagnostic decoders/counters/cap and format literals','IOUC timing timestamps, atomic IDs and paired printf after unlock; no unchanged driver re-review']},indent=2)+'\n');print('PREPARED; no artifact inspected');raise SystemExit(0)
assert z.source and z.kernel_sha and z.kernel_bytes
CI=R/f'research/ci-{z.ci}';run=json.loads((CI/'run.json').read_text());assert run['status']=='completed' and run['conclusion']=='success' and run['headSha']==z.source
k=CI/'xnu-kernel/kernel.development.sweet';assert k.stat().st_size==z.kernel_bytes and sha(k)==z.kernel_sha
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k);records=[]
for label,pattern,exact in FUNCTIONS:
 start,end,symbol=m.bounds(pattern,exact);body=m.read(start,end-start);(P/(label+'.bin')).write_bytes(body);(P/(label+'.s')).write_text(m.dump(start,end));records.append({'label':label,'symbol':symbol,'start':hex(start),'end':hex(end),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()})
(P/'extraction.json').write_text(json.dumps({'status':'EXTRACTED_PENDING_MANUAL_REVIEW','ci':z.ci,'source':z.source,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'functions':records,'canonical_source_pins':{s:sha(R/'shim/nord2-xnu'/s) for s in SOURCES}},indent=2)+'\n')
