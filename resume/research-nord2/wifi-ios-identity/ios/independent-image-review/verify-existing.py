#!/usr/bin/env python3
"""Independent existing-image invocation and bounded artifact checks. No device."""
from pathlib import Path
import hashlib,importlib.util,json,resource,struct,subprocess,sys
sys.dont_write_bytecode=True
P=Path(__file__).resolve().parent;S=P.parent;R=next(p for p in S.parents if(p/'AGENTS.md').exists())
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
identity=json.loads((S/'identity-required.json').read_text());schema=json.loads((S/'native-schema-required.json').read_text())
assert identity['parent_verified'] and schema['parent_verified'] and not schema['schema_pending']
assert identity['ci_run']==39804982005 and identity['ci_attempt']==2
assert identity['source_commit']=='6b3302d1e039ca451b02a8aa2d8773aea269d8e4'
assert identity['kernel_sha256']=='7f363c332d1c8773d0141339154dc99abd2054615f74319c2f6c04fbe6b3ce14'
ci=json.loads((R/identity['ci_run_json']).read_text())
assert (ci['databaseId'],ci['attempt'],ci['status'],ci['conclusion'],ci['headSha'])==(identity['ci_run'],2,'completed','success',identity['source_commit'])
for n in ['manifest.json','payload.bin','image-build.log']:assert (S/n).is_file(),'builder pending: '+n
assert 'FAILURES: none' in (S/'image-build.log').read_text()
protected=['verify-image.py','identity-required.json','native-schema-required.json','prepared-image-required.json','authorization-required.json','baseline.json','recovery-image.json','manifest.json','payload.bin','image-build.log','build-image.sh','run-approved-boot.py']
pins={n:sha(S/n) for n in protected}
assert not(S/'RUNNER_EXECUTION_USED').exists()
prepared=json.loads((S/'prepared-image-required.json').read_text());approval=json.loads((S/'authorization-required.json').read_text())
assert not prepared['parent_verified']
assert all(approval[k] is False for k in ['human_exact_image_authorized','rf_calibration_authorized','conditional_recovery_authorized'])
assert approval['image_sha256'] is None and approval['device_serial'] is None
# Limit this process and all compiler/verifier children to 1GiB address space.
resource.setrlimit(resource.RLIMIT_AS,(1<<30,1<<30))
with (P/'verify-existing.log').open('x') as log:
 result=subprocess.run([sys.executable,'-B',str(S/'verify-image.py'),'--check-existing'],cwd=R,stdout=log,stderr=subprocess.STDOUT)
assert result.returncode==0,'existing-image verifier failed; see private log'
assert pins=={n:sha(S/n) for n in protected},'stage artifacts/gates changed during independent verification'
manifest=json.loads((S/'manifest.json').read_text());image=R/manifest['image'];kernel=R/identity['kernel_path']
assert image.name=='nord2-ios-wifi-identity-keychain-stop23.img'
assert image.stat().st_size==manifest['image_bytes'] and image.stat().st_size<=33554432
assert sha(image)==manifest['image_sha256'] and sha(kernel)==identity['kernel_sha256']
assert kernel.stat().st_size==identity['kernel_bytes']==17703208
assert manifest['ci_run']==identity['ci_run'] and manifest['expected_kernel_source']==identity['source_commit']
assert manifest['boot_spare_bytes']==33554432-image.stat().st_size and manifest['boot_spare_bytes']>0
assert manifest['hardware_tested'] is False
payload=(S/'payload.bin').read_bytes();assert hashlib.sha256(payload).hexdigest()==manifest['payload_sha256']
sys.path.insert(0,str(R/'tools'));import adt
extract=lambda b:b[struct.unpack_from('<Q',b,32)[0]:sum(struct.unpack_from('<QQ',b,32))]
actual=extract(payload);tree,used=adt.parse(actual);assert used==len(actual) and adt.serialize(tree)==actual
chosen=[x for x in tree.children if x.name=='chosen'];assert len(chosen)==1;chosen=chosen[0]
cal=(R/'research/nord2/wifi/calibration/WIFI').read_bytes()
assert len(cal)==6146 and hashlib.sha256(cal).hexdigest()=='0000000000000000000000000000000000000000000000000000000000000000'
assert [(f,v) for k,f,v in chosen.props if k=='nord2-wifi-calibration']==[(0,cal)]
assert [(f,v) for k,f,v in chosen.props if k=='mac-address-wifi0']==[(0,cal[4:10])]
chosen.props=[x for x in chosen.props if x[0]!='mac-address-wifi0']
baseline=json.loads((S/'baseline.json').read_text());old=R/baseline['identity_baseline_payload_path']
assert sha(old)==baseline['identity_baseline_payload_sha256'];old_adt=extract(old.read_bytes())
assert len(old_adt)==19160 and hashlib.sha256(old_adt).hexdigest()==baseline['identity_baseline_adt_sha256']
assert adt.serialize(tree)==old_adt
recovery=json.loads((S/'recovery-image.json').read_text());restore=R/recovery['path']
assert recovery['bytes']==33462272 and recovery['sha256']=='6e4ce860cfbd0b27fbace65e61e96edc8d2af8067603652cd67a0939c53331c6'
assert restore.stat().st_size==recovery['bytes'] and sha(restore)==recovery['sha256']
assert baseline['fallback_image_sha256']==recovery['sha256'] and baseline['fallback_image_bytes']==recovery['bytes']
rescue=R/'research/nord2/rescue/nord2-adb-rescue.img';assert rescue.stat().st_size==28884992 and sha(rescue)==baseline['ram_rescue_sha256']=='d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'
assert not(S/'RUNNER_EXECUTION_USED').exists()
report={'verdict':'PASS_EXISTING_IMAGE_OFFLINE_ONLY','image':str(image.relative_to(R)),'image_bytes':image.stat().st_size,'image_sha256':manifest['image_sha256'],'boot_capacity_bytes':33554432,'boot_spare_bytes':manifest['boot_spare_bytes'],'payload_sha256':manifest['payload_sha256'],'kernel_sha256':identity['kernel_sha256'],'kernel_bytes':17703208,'source_commit':identity['source_commit'],'CI':39804982005,'attempt':2,'exact_raw6_flags0_equals_pinned_calibration':True,'remove_only_identity_recovers_executed_6e4_ADT_byte_exact':True,'recovery_current_6e4_verified':True,'rescue_d78_verified':True,'identity_native_gates_verified':True,'prepared_gate_during_review':False,'human_RF_recovery_approval_during_review':False,'runner_unused':True,'stage_artifacts_and_gates_unchanged':True,'address_space_limit_bytes':1<<30,'verifier_children_max_rss_kib':resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,'device_access':False,'builder_or_runner_invoked':False,'limitations':['Offline existing-image verification and source/linked pins; no live MG, crash repair, keychain, association, authorization, DHCP/IP or Internet proof','Native registry/source equality was separately reviewed; this is not a live measurement of owner address','No raw calibration/address bytes printed'],'stage_pins':pins,'native_registry_source_sha256':sha(R/'shim/nord2-xnu/wifi_ios_registry.cpp'),'identity_tool_sha256':sha(R/'tools/nord2-wifi-identity.py')}
(P/'report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:report[k] for k in ['verdict','image_bytes','image_sha256','boot_spare_bytes','source_commit','kernel_sha256','CI','attempt']},indent=2))
