#!/usr/bin/env python3
"""Read-only closed-stage checks. Never imports or executes the device runner."""
from pathlib import Path
import ast,hashlib,json,importlib.util
P=Path(__file__).resolve().parent.parent;O=P/'offline-review';R=next(p for p in P.parents if (p/'AGENTS.md').exists())
checks=[]
for n in ('verify-image.py','run-approved-boot.py','parse-backend.py','parse-ios-service.py'):compile((P/n).read_text(),str(P/n),'exec');checks.append(n+' syntax')
for n,fields in {'identity-required.json':['parent_verified'],'native-schema-required.json':['parent_verified'],'prepared-image-required.json':['parent_verified'],'authorization-required.json':['human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized'],'stage-pending.json':['source_verified','prepared_verified','image_authorized','runner_used','hardware_tested']}.items():
 j=json.loads((P/n).read_text());assert all(j[k]is False for k in fields);checks.append(n+' gates false')
for n in ('manifest.json','payload.bin','RUNNER_EXECUTION_USED','flash.log','client-hardware.log','client-hardware.pid','hardware-live'):assert not(P/n).exists()
checks.append('no prepared image/evidence/runner execution state')
s=importlib.util.spec_from_file_location('closed_verify_only',P/'verify-image.py');v=importlib.util.module_from_spec(s);s.loader.exec_module(v)
assert len(v.BASE_COMMANDLINE.encode())==474 and len(v.COMMANDLINE.encode())==539 and v.COMMANDLINE.endswith(' nord2wifiios=1')
tokens=v.COMMANDLINE.split();keys=[x.split('=')[0]for x in tokens if'='in x];assert len(keys)==len(set(keys))
assert all(x in tokens for x in ('nord2ufsprofile=1','nord2fbdiag=1','nord2ufs2live=1','nord2wifimain=1','nord2wificap=1','nord2wifibasic=1','nord2wifiscan=1','nord2wifivcn33=1','nord2wifiios=1'))
for fn in (v.load_identity,v.load_schema):
 try:fn()
 except AssertionError as e:checks.append('blocked as expected: '+str(e))
 else:raise AssertionError('closed gate unexpectedly admitted')
j=json.loads((P/'recovery-image.json').read_text());assert j['sha256']=='52d4b1591b3b8d906214ec37bc93e309d5a3b69d59fd8a013c424d755a504aca'and j['bytes']==33415168
f=R/j['path'];assert f.stat().st_size==j['bytes']and hashlib.sha256(f.read_bytes()).hexdigest()==j['sha256'];checks.append('exact healthy52d4 recovery file identity')
runner=(P/'run-approved-boot.py').read_text();assert runner.index('with (OUT / "RUNNER_EXECUTION_USED").open("x")')<runner.index('devices = subprocess.run')
assert 'run("flash", "boot", str(verify.IMAGE))' in runner and 'run("reboot")' in runner;assert '"erase"'not in runner and '"userdata"'not in runner and '"set_active"'not in runner
checks.append('one-use marker before USB; exact boot-partition flash+reboot only; runner NOT executed')
metadata=v.request_metadata();assert metadata['wifi_association_requested']and metadata['wifi_wpa2_psk_ccmp_requested']and metadata['wifi_bsd_ethernet_requested']and metadata['wifi_first_connection_only'];assert not metadata['hardware_tested']and not metadata['wifi_sae_supported']and not metadata['wifi_required_pmf_supported']and not metadata['wifi_private_address_supported'];checks.append('requested connection scope distinct from hardware/readiness')
report={'status':'PASS_CLOSED_OFFLINE_CHECKS_ONLY','checks':checks,'commandline_bytes':539,'pins':{n:hashlib.sha256((P/n).read_bytes()).hexdigest()for n in ('build-image.sh','verify-image.py','run-approved-boot.py','recovery-image.json','baseline.json')},'limits':'No builder/runner/device/CI execution, no false gate promotion; identity/source/markers are root pending. This script refuses after intentional gates/state change and is an initial closed-template check, not final image verifier.'}
(O/'closed-stage-tests.json').write_text(json.dumps(report,indent=2)+'\n');print('PASS',len(checks),'closed checks; no builder/runner/device invocation')
