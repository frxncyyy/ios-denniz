#!/usr/bin/env python3
"""Bounded changed-callable extraction; run only after root artifact-ready notice."""
from pathlib import Path
import hashlib,json,runpy
R=next(p for p in Path(__file__).resolve().parents if (p/'shim/nord2-xnu').is_dir());P=Path(__file__).parent
CI=R/'research/ci-39823157589';HEAD='1134851bf309c06c613b0383842e70a895e46473'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
run=json.loads((CI/'run.json').read_text());assert run['status']=='completed' and run['conclusion']=='success' and run['headSha']==HEAD
k=CI/'xnu-kernel/kernel.development.sweet';expected=next(l.split()[0] for l in (k.parent/'SHA256SUMS').read_text().splitlines() if l.split()[-1].lstrip('*')==k.name);assert sha(k)==expected
M=runpy.run_path(str(R/'research/nord2/wifi-ios-profile/ios/linked-review/review.py'))['MachO'];m=M(k)
functions=[('join_decode','_niwj_decode_identity',True),('client_join','NiwlLifecycleClient::joinLocked(',False),('client_dispatch','NiwlLifecycleClient::dispatchLocked(',False),('native_disconnect','_nord2_wifi_join_disconnect',True),('native_retry','_nst_peer_begin',True)]
records=[]
for label,pattern,exact in functions:
 a,e,s=m.bounds(pattern,exact);body=m.read(a,e-a);(P/(label+'.bin')).write_bytes(body);(P/(label+'.s')).write_text(m.dump(a,e));records.append({'label':label,'symbol':s,'start':hex(a),'end':hex(e),'bytes':len(body),'sha256':hashlib.sha256(body).hexdigest()})
sources=['wifi_ios_join.c','wifi_ios_join.h','wifi_ios_client.cpp','wifi_station_service_bind.inc','wifi_station_transport.c']
(P/'extraction.json').write_text(json.dumps({'ci':39823157589,'source':HEAD,'kernel_sha256':sha(k),'kernel_bytes':k.stat().st_size,'functions':records,'canonical_source_hashes':{s:sha(R/'shim/nord2-xnu'/s) for s in sources},'status':'EXTRACTED_REQUIRES_BOUNDED_MANUAL_REVIEW','scope':'Only changed decode/trace/split prints, SET22 and disconnect guards, Retry mask; source equality and generic ISA delegated to root'},indent=2)+'\n')
