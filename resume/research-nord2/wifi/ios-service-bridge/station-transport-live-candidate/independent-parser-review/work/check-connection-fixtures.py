#!/usr/bin/env python3
"""Validate saved actual emitted host-fixture rows, with no native execution."""
from pathlib import Path
import hashlib,importlib.util,json,re
P=Path(__file__).resolve().parent;E=P/'connection-evidence'
s=importlib.util.spec_from_file_location('receipt',P/'parse-connection.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
raw=(E/'service-fixture.log').read_bytes();assert hashlib.sha256(raw).hexdigest()=='130d20268abc8b07bb3fca2f49aa8542cdf89441b617f26c5afafaf795a997e4'
whole=m.parse(raw);assert not whole['valid']and whole['issues']==['contradictory authorized station snapshot']
# g3 is the author's explicitly inconsistent synthetic seeded-ready seam, not a
# trace of reached readiness. Do not repair, reinterpret or count it as success.
coherent=b''.join(line for line in raw.splitlines(keepends=True)if not line.startswith(b'NORD2 WIFI IOS STATION: generation=3 '))
r=m.parse(coherent);assert r['valid'],r
assert not r['native_authorized_observed']and not r['current_authorized_link_observed']and not r['join_notification_observed']
assert len(r['attempts'])==5 and all(not x['dhcp_exchange_proven']for x in [r])
for g in (1,2,4,5,6):
 block=b''.join(line for line in raw.splitlines(keepends=True)if re.search(rb'generation='+str(g).encode()+rb'\b',line));v=m.parse(block);assert v['valid'],v
 assert not v['current_authorized_link_observed']and not v['ip_acquisition_proven']
report={'status':'PASS_ACTUAL_EMITTER_HOST_FIXTURE_ONLY','native_manifest_sha256':'5000d7cf85e5e5c7e5e356fb05717a6b6c8e0987abe939f2717e3f8d718671e9','fixture_sha256':hashlib.sha256(raw).hexdigest(),'intentional_inconsistent_seed_generation':3,'raw_negative':whole,'coherent_rows_result':r,'independently_validated_generation_blocks':5,'limits':'Actual compiled binder printf output; hardware/native IO mocked and ready states synthetically seeded. No authorization chain, hardware association, TX data outcome or IP proof manufactured.'}
(E/'actual-fixture-report.json').write_text(json.dumps(report,indent=2)+'\n');print(report['status'],5,'generation blocks; intentional impossible seed refused')
