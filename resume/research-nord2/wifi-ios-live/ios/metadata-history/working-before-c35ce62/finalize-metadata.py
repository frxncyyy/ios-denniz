#!/usr/bin/env python3
"""Pin current private stage inputs; never open parent/prepared/human gates.

--prepare-current records mutable working-source hashes without source/CI proof.
--source verifies every source pin against that exact committed Git object.
Optional --ci-run/--ci-attempt/--kernel-sha256/--kernel-bytes checks completed
local artifacts only. Root separately reviews/promotes identity/schema gates.
No build, GH/network, download, device, runner or image operation is invoked.
"""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').is_file())
L=R/'research/nord2/wifi/live-control-20261023'
def load(p):return json.loads(p.read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def put(n,o):(P/n).write_text(json.dumps(o,indent=2)+'\n')
def pin(p):return {'path':str(p.relative_to(R)),'sha256':sha(p)}
def path(n):
 p=Path(n);assert not p.is_absolute() and '..' not in p.parts;return R/p
ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--prepare-current',action='store_true');ap.add_argument('--source');ap.add_argument('--ci-run',type=int);ap.add_argument('--ci-attempt',type=int);ap.add_argument('--kernel-sha256');ap.add_argument('--kernel-bytes',type=int);ap.add_argument('--linked-review',action='append',default=[]);a=ap.parse_args()
assert a.prepare_current or a.source,'Select mutable snapshot or exact committed source'
assert a.ci_run is not None or all(x is None for x in (a.ci_attempt,a.kernel_sha256,a.kernel_bytes)), 'Artifact fields require an exact CI run'
prepared=load(P/'prepared-image-required.json');approval=load(P/'authorization-required.json')
assert prepared['parent_verified'] is False and all(approval[k] is False for k in('human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized')),'Never rewrite metadata after prepared/approval gates open'
protected={n:sha(P/n) for n in('prepared-image-required.json','authorization-required.json')}
old=load(R/'research/nord2/wifi-ios-dhcp/ios/source-proposals.json');names={x['path'] for x in old['source_pins']}
# Preserve the previous reviewed source set and add new actual canonical files,
# including newly untracked proposals without pretending they are committed.
for pattern in ('shim/nord2-xnu/wifi_live_control.*','shim/nord2-xnu/wifi_live_control_bind.inc','shim/nord2-xnu/wifi_station_rejoin.h','tools/nord2-wifi-live.py','tools/nord2-wifi-tx-correlation.py','tools/test-nord2-wifi-live*.py','tools/test-nord2-wifi-rejoin.py','tools/test-nord2-wifi-tx-correlation.py','tools/fixtures/nord2-wifi-live*/*','tools/fixtures/nord2-wifi-rejoin/*','tools/fixtures/nord2-wifi-tx-correlation/*','tools/nord2-check-usb-control.py'):
 names|={str(p.relative_to(R)) for p in R.glob(pattern) if p.is_file() and '__pycache__' not in p.parts}
source_pins=[pin(path(n)) for n in sorted(names)]
committed=False
if a.source:
 assert re.fullmatch('[0-9a-f]{40}',a.source)
 for item in source_pins:
  b=subprocess.check_output(['git','show',a.source+':'+item['path']],cwd=R);assert hashlib.sha256(b).hexdigest()==item['sha256'],'Working source differs from commit: '+item['path']
 committed=True
review_names=[
 L/'mailbox-review/review.json',L/'mailbox-review/manifest.json',
 L/'mailbox-review/v2/review.json',L/'mailbox-review/v2/manifest.json',
 L/'rejoin-review/review.json',L/'rejoin-review/manifest.json',
 L/'host-v2/manifest.json',
 R/'research/nord2/wifi/upstream-dhcp-20261023/tx-correlation-candidate/manifest.json',
 R/'research/nord2/wifi/upstream-dhcp-20261023/tx-correlation-candidate/independent-review.json']
reviews=[pin(p) for p in review_names if p.is_file()]
local_names=[L/'validation'/n for n in ('mailbox/report.json','correlation/report.json','station/report.json','host-final/report.json','arm64-initial/report.json','arm64-final/report.json','rejoin-final/test-native.log','binder-final/report.json')]
local=[dict(pin(p),scope='Actual saved receipt only; parent must bind final composed source coverage') for p in local_names if p.is_file()]
missing=[str(p.relative_to(R)) for p in local_names if not p.is_file()]
identity=load(P/'identity-required.json');identity={k:(False if k=='parent_verified' else None) for k in identity}
ci_ready=False
if a.ci_run is not None:
 assert committed and a.ci_run>0 and a.ci_attempt and a.ci_attempt>0 and a.kernel_bytes and a.kernel_bytes>0
 assert re.fullmatch('[0-9a-f]{64}',a.kernel_sha256 or '')
 ci=R/f'research/ci-{a.ci_run}';run=load(ci/'run.json');k=ci/'xnu-kernel/kernel.development.sweet'
 assert (run['status'],run['conclusion'],run['headSha'],run['databaseId'],run['attempt'])==('completed','success',a.source,a.ci_run,a.ci_attempt)
 assert k.stat().st_size==a.kernel_bytes and sha(k)==a.kernel_sha256
 sums=[l.split()[0] for l in (k.parent/'SHA256SUMS').read_text().splitlines() if len(l.split())==2 and Path(l.split()[1].lstrip('*')).name==k.name];assert sums==[a.kernel_sha256]
 identity.update(source_commit=a.source,ci_run=a.ci_run,ci_attempt=a.ci_attempt,kernel_path=str(k.relative_to(R)),kernel_sha256=a.kernel_sha256,kernel_bytes=a.kernel_bytes,ci_run_json=str((ci/'run.json').relative_to(R)))
 ci_ready=True
linked=[pin(path(n)) for n in a.linked_review]
assert not linked or ci_ready,'Actual linked receipts require the successful exact artifact identity'
# Do not infer review PASS from mere file presence. Parent opens review booleans
# after inspecting these exact receipts and the final artifact audit.
proposal=load(P/'source-proposals.json');proposal.update(parent_verified=False,proposal_reviewed=False,source_verified=committed,source_commit=a.source,source_tree_verified=committed,source_pins=source_pins,reviews=reviews,local_verification=local,ci_identity_pending=not ci_ready,linked_identity_pending=not bool(linked),ci_run=a.ci_run,ci_attempt=a.ci_attempt,kernel_sha256=a.kernel_sha256,experimental_rejoin_requested=True,rejoin_hardware_proven=False,dhcp_proven=False)
proposal['required_gates']={k:False for k in proposal['required_gates']};put('source-proposals.json',proposal)
schema=load(P/'native-schema-required.json');schema.update(parent_verified=False,schema_pending=True,source_pins=source_pins,candidate_manifest_sha256=sha(P/'source-proposals.json'),review_evidence=reviews,local_verification=local,linked_reviews=linked,ci_identity_pending=not ci_ready,linked_identity_pending=not bool(linked),experimental_rejoin_requested=True,rejoin_hardware_proven=False)
schema['required_gates']={k:False for k in schema['required_gates']}
readers=['tools/nord2-wifi-connection-log.py','tools/nord2-wifi-wpa-diag.py','tools/nord2-wifi-data-diag.py','tools/nord2-wifi-query-timing.py','tools/nord2-wifi-station-diag.py','tools/nord2-wifi-rx-error.py','tools/nord2-wifi-tx-correlation.py','tools/nord2-wifi-live.py']
schema['connection_diagnostics']=[pin(R/n) for n in readers]
put('identity-required.json',identity);put('native-schema-required.json',schema)
assert protected=={n:sha(P/n) for n in protected}
put('metadata-preparation-report.json',{'state':'CLOSED_CURRENT_SOURCE_PINS_PREPARED','source_pins':len(source_pins),'committed_source_verified':committed,'CI_artifact_identity_checked':ci_ready,'parent_schema_prepared_human_opened':False,'source_review_receipts':reviews,'local_receipts':local,'missing_final_receipts':missing,'linked_receipts':linked,'DHCP_lease_proven':False,'rejoin_hardware_proven':False,'EP0_note':'Sources/executables exist under validation/ep0 but no saved machine-readable result is claimed by this script','additional_required_actual_evidence':['Final composed station/service/binder quota and join_ready native receipts','Exact successful generated source equality + actual generic ARM64 compile including wifi_live_control + complete ISA audit','Independent actual linked mailbox/owner/rejoin/PID review bound to kernel SHA','Independent existing-image proof, then new exact-image human/RF/recovery authorization'],'critical_pins':{n:sha(P/n) for n in('identity-required.json','native-schema-required.json','source-proposals.json','prepared-image-required.json','authorization-required.json')}})
print('Prepared',len(source_pins),'source pins; committed=',committed,'CI=',ci_ready,'ALL parent/prepared/human gates remain CLOSED')
