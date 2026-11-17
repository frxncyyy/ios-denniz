#!/usr/bin/env python3
"""Offline closed-stage checks. Never invokes runner, builder, or hardware."""
from pathlib import Path
import ast,hashlib,importlib.util,json,subprocess,sys
P=Path(__file__).resolve().parent
ROOT=P.parents[3]
spec=importlib.util.spec_from_file_location('basic_verify',P/'verify-image.py');v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
old=json.loads((ROOT/'research/nord2/wifi-capability-format/ios/manifest.json').read_text())
assert len(v.BASE_COMMANDLINE.encode())==474 and v.BASE_COMMANDLINE==old['commandline']
assert v.COMMANDLINE==v.BASE_COMMANDLINE+' nord2wifibasic=1' and len(v.COMMANDLINE.encode())==491
assert v.IMAGE.name=='nord2-ios-wifi-basic-config-battery-stop23.img'
for name,flag in [('identity-required.json','parent_verified'),('native-schema-required.json','parent_verified'),('prepared-image-required.json','parent_verified'),('authorization-required.json','human_exact_image_authorized')]:
 assert json.loads((P/name).read_text())[flag] is False
for f in ('manifest.json','payload.bin','image-build.log','flash.log','client-hardware.pid','client-hardware.log','hardware-live','RUNNER_EXECUTION_USED'):
 assert not (P/f).exists(),f
for f in P.glob('*.py'):ast.parse(f.read_text(),filename=str(f))
subprocess.run(['sh','-n',str(P/'build-image.sh')],check=True)
r=subprocess.run([sys.executable,str(P/'verify-image.py'),'--preflight'],capture_output=True,text=True)
assert r.returncode and 'BLOCKED: parent has not supplied' in r.stderr
# Runner syntax/policy inspection only: never execute it, even behind false gates.
t=(P/'run-approved-boot.py').read_text()
assert '--authorized-wifi-basic-config-calibration-boot' in t
assert 'RUNNER_EXECUTION_USED' in t and '.open("x")' in t
assert 'wifi_basic_config_response_expected' in t and 'device-readback.sha256' in t
assert 'e0d3a50f' in (P/'baseline.json').read_text()
print('PASS closed gates, exact474B baseline, unique one-use runner, shell/Python syntax; verifier refuses before operations')
