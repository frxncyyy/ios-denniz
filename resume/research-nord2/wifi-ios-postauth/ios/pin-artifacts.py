from pathlib import Path
import json,hashlib
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').is_file());P=Path(__file__).parent;C=R/'research/ci-39850764280';SOURCE='aecd95ca388b6b7c6d61712ae22e8eb24ddfe3a6'
sha=lambda b:hashlib.sha256(b).hexdigest()
def load(p):return json.loads(p.read_text())
def pin(p):
 b=p.read_bytes();return {'path':str(p.relative_to(R)),'sha256':sha(b),'bytes':len(b)}
r=load(C/'run.json');assert (r['databaseId'],r['attempt'],r['headSha'],r['status'],r['conclusion'])==(39850764280,1,SOURCE,'completed','success')
k=C/'xnu-kernel/kernel.development.sweet';kp=pin(k)
assert [x.split()[0] for x in (k.parent/'SHA256SUMS').read_text().splitlines() if x.split()[-1].lstrip('*')==k.name]==[kp['sha256']]
assert load(C/'source-buildtree-audit.json')['passed'] is True
assert all(x['generic_all'] for x in load(C/'normal-arm64-audit.json').values())
assert 'FAIL' not in (C/'isa-audit.log').read_text()
src=load(P/'source-proposals.json');assert src['source_verified'] and src['source_commit']==SOURCE
for f in src['source_pins']+src['reviews']:
 assert sha((R/f['path']).read_bytes())==f['sha256'],f['path']
linked=P/'linked-review';lm=load(linked/'manifest.json')
# Root manually checks the linked review verdict and every manifest entry before invocation.
assert (linked/'root-verified.json').is_file()
rv=load(linked/'root-verified.json');assert rv['passed'] and rv['kernel_sha256']==kp['sha256'] and rv['source_commit']==SOURCE
assert rv['review_sha256']==sha((linked/'review.json').read_bytes()) and rv['manifest_sha256']==sha((linked/'manifest.json').read_bytes())
markers=load(P/'native-schema-required.json')
assert all(x.encode() in k.read_bytes() for x in markers['required_kernel_markers']+markers['required_new_diagnostic_markers'])
identity={'parent_verified':True,'ci_run':39850764280,'ci_attempt':1,'source_commit':SOURCE,'kernel_path':kp['path'],'kernel_sha256':kp['sha256'],'kernel_bytes':kp['bytes'],'ci_run_json':str((C/'run.json').relative_to(R))}
(P/'identity-required.json').write_text(json.dumps(identity,indent=2)+'\n')
markers.update(parent_verified=True,schema_pending=False,local_submission_reviewed=True,local_submission_readers_reviewed=True,scan_resume_requested=True,scan_resume_reviewed=True,source_pins=src['source_pins'],candidate_manifest_path=str((P/'source-proposals.json').relative_to(R)),candidate_manifest_sha256=sha((P/'source-proposals.json').read_bytes()))
markers['connection_diagnostics']=[pin(R/'tools'/name) for name in ['nord2-wifi-connection-log.py','nord2-wifi-query-timing.py','nord2-wifi-station-diag.py','nord2-wifi-wpa-diag.py','nord2-wifi-data-diag.py']]
markers['linked_reviews']=[pin(linked/name) for name in ['review.json','manifest.json']]+[pin(C/name) for name in ['source-buildtree-audit.json','normal-arm64-audit.json','isa-audit.log']]
(P/'native-schema-required.json').write_text(json.dumps(markers,indent=2)+'\n')
print(json.dumps(identity))
