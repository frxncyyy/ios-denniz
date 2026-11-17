from pathlib import Path
import importlib.util,json,ast,subprocess,sys
P=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('vcnverify',P/'verify-image.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
old=P.parents[1]/'wifi-rx-lifecycle/ios/verify-image.py'
spec=importlib.util.spec_from_file_location('oldverify',old);o=importlib.util.module_from_spec(spec);spec.loader.exec_module(o)
assert v.COMMANDLINE==o.COMMANDLINE+' nord2wifivcn33=1'
assert v.COMMANDLINE.split().count('nord2wifivcn33=1')==1
assert all(x in v.COMMANDLINE.split() for x in ('ufsrw=1','rootrw=1','nord2ufs2live=1'))
for f,k in [('identity-required.json','parent_verified'),('native-schema-required.json','parent_verified'),('prepared-image-required.json','parent_verified'),('authorization-required.json','human_exact_image_authorized')]:
 assert json.loads((P/f).read_text())[k] is False
for key in ('rf_calibration_authorized','conditional_recovery_authorized'):
 assert json.loads((P/'authorization-required.json').read_text())[key] is False
assert json.loads((P/'authorization-required.json').read_text())['device_serial'] is None
for f in ('RUNNER_EXECUTION_USED','image-build.log','hardware-live','flash.log','client-hardware.log','payload.bin','manifest.json'):
 assert not (P/f).exists(),f
r=subprocess.run([sys.executable,str(P/'verify-image.py'),'--preflight'],capture_output=True,text=True)
assert r.returncode and 'parent has not supplied' in r.stderr
runner=(P/'run-approved-boot.py').read_text();ast.parse(runner)
assert '["--authorized-wifi-vcn33-calibration-boot"]' in runner
assert 'RUNNER_EXECUTION_USED' in runner and '.open("x")' in runner
assert 'userdata_write_completed' in runner and 'full_readback_verified' in runner
assert 'device-readback.sha256' in runner and 'is-userspace' in runner
assert 'c31ff5e47611fac18d6eb242cab86f39fa049d751ba0a9e0fa65ed3658ad8b1f' in runner
print(json.dumps({'closed_stage_test':'PASS','old_commandline_bytes':len(o.COMMANDLINE.encode()),'new_commandline_bytes':len(v.COMMANDLINE.encode()),'runner_executed':False,'image_built':False}))
