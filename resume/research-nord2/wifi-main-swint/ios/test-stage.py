#!/usr/bin/env python3
"""Offline checks only: the fresh SWINT stage has no executable approval."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

p=Path(__file__).resolve().parent
r=p.parents[3]
old=json.loads((r/'research/nord2/wifi-main-ownerdiag/ios/manifest.json').read_text())
old_sha='e011b66da1b49367f22dfb1d01cedc7712f306f98673752c46c05fa9c08fb4b7'
assert old['image_sha256']==old_sha
identity=json.loads((p/'identity-required.json').read_text())
prepared=json.loads((p/'prepared-image-required.json').read_text())
approval=json.loads((p/'authorization-required.json').read_text())
schema=json.loads((p/'native-schema-required.json').read_text())
baseline=json.loads((p/'baseline.json').read_text())
assert identity=={'parent_verified':False,'ci_run':None,'source_commit':None,
                  'kernel_path':None,'kernel_sha256':None,'ci_run_json':None}
assert prepared=={'parent_verified':False,'image_sha256':None,'image_bytes':None,'image_path':None}
assert approval['human_exact_image_authorized'] is False
assert approval['rf_calibration_authorized'] is False
assert approval['conditional_recovery_authorized'] is False
assert approval['image_sha256'] is None and approval['device_serial'] is None
assert approval['authorized_at'] is None and approval['authorization_evidence'] is None
assert approval['scope']=='boot-with-main-wifi-swint-calibration'
assert schema['parent_verified'] is False
assert schema['swint_candidate_manifest_path']=='research/nord2/wifi/next-stage/main-fw-transport/mcu-swint-enable-v2/manifest.json'
assert schema['swint_candidate_manifest_sha256']=='fbb8a9cf5b791d8cd3674c85bff59eee259a23963fe985251bb5c40be57672d8'
review_bytes=(r/schema['swint_candidate_manifest_path']).read_bytes()
assert hashlib.sha256(review_bytes).hexdigest()==schema['swint_candidate_manifest_sha256']
review=json.loads(review_bytes)
assert review['changed_runtime']==['wifi_main_wfdma_bind.inc']
assert review['unchanged_header_coordinator'] is True
assert schema['swint_source_pins']==[{'path':'shim/nord2-xnu/wifi_main_wfdma_bind.inc',
    'sha256':review['files']['wifi_main_wfdma_bind.inc']}]
assert schema['swint_required_markers']==[]
assert schema['wfdma_native_sha256']==review['files']['wifi_main_wfdma_bind.inc']
assert schema['rxformat_native_finish_source_sha256']==review['files']['wifi_main_wfdma_bind.inc']
assert schema['ownerdiag_source_pins'][1]['sha256']==review['baseline']['wifi_main_wfdma_bind.inc']
assert schema['required_new_diagnostic_markers'][-1]=='NORD2 WIFI MAIN GUARD:'
assert baseline['fallback_image_sha256']=='e0d3a50f7a706f27a8d9fb19bbd6951c85f016e8b179b47b726c331331ec65d5'
assert baseline['ram_rescue_sha256']=='d78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5'
assert baseline['future_boot_arg']=='nord2wifimain=1'
build=(p/'build-image.sh').read_text()
verify=(p/'verify-image.py').read_text()
runner=(p/'run-approved-boot.py').read_text()
image='nord2-ios-wifi-main-swint-battery-stop23.img'
assert image in build and image in verify
assert '--enable-nord2wifimain' in build
assert '"$temp/main.adt" "$temp/payload.bin"' in build
assert old['commandline']==__import__('runpy').run_path(str(p/'verify-image.py'))['COMMANDLINE']
assert '--authorized-wifi-main-swint-calibration-boot' in runner
assert '"boot-with-main-wifi-swint-calibration"' in runner
assert old_sha in runner and old_sha in verify
assert 'approval["conditional_recovery_authorized"] is True' in runner
assert 'swint_candidate_manifest_path' in verify and 'swint_source_pins' in verify
deps=json.loads((p/'parser-dependencies.json').read_text())
for entry in deps['copied_parser_sources']:
    assert entry['path'].startswith('research/nord2/wifi-main-swint/ios/')
    assert entry['executed_stage_path'].startswith('research/nord2/wifi-main-ownerdiag/ios/')
    h=lambda path: hashlib.sha256((r/path).read_bytes()).hexdigest()
    assert h(entry['path'])==entry['sha256']==entry['executed_stage_sha256']==h(entry['executed_stage_path'])
for name in ('manifest.json','payload.bin','image-build.log','flash.log',
             'client-hardware.log','client-hardware.pid','hardware-live',
             'authorization-used.json'):
    assert not (p/name).exists(), name
assert not list(p.glob('*.img'))
for argv in ((sys.executable,str(p/'verify-image.py'),'--preflight'),
             (sys.executable,str(p/'run-approved-boot.py'),
              '--authorized-wifi-main-swint-calibration-boot')):
    result=subprocess.run(argv,cwd=r,capture_output=True,text=True)
    assert result.returncode != 0 and 'BLOCKED: parent has not supplied and verified CI identity' in result.stderr
print('PASS: private SWINT stage has closed gates, new scope, old image rejected; no device actions')
