#!/usr/bin/env python3
"""Bounded closed-gate checks for the offline capability stage."""
from pathlib import Path
import hashlib
import importlib.util
import json
import subprocess
import sys

out = Path(__file__).resolve().parent
root = out.parents[3]
spec = importlib.util.spec_from_file_location('capability_verify', out / 'verify-image.py')
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)

baseline = json.loads((out/'baseline.json').read_text())
assert baseline['future_boot_arg'] == 'nord2wificap=1'
assert 'nord2wifimain=1' in baseline['existing_required_boot_args']
assert len(verify.COMMANDLINE.encode()) == 474 < 608
assert 'nord2ufsprofile=1' in verify.COMMANDLINE.split()
assert 'nord2fbdiag=1' in verify.COMMANDLINE.split()
oldspec=importlib.util.spec_from_file_location('executed_capability_verify',root/'research/nord2/wifi-capability/ios/verify-image.py')
oldverify=importlib.util.module_from_spec(oldspec);oldspec.loader.exec_module(oldverify)
assert verify.COMMANDLINE == oldverify.COMMANDLINE
assert verify.COMMANDLINE.endswith(' nord2wifimain=1 nord2wificap=1')
assert verify.COMMANDLINE.count('nord2wificap=1') == 1
assert verify.IMAGE.name == 'nord2-ios-wifi-capability-format-battery-stop23.img'
assert (out / 'identity-required.json').exists()
identity = json.loads((out / 'identity-required.json').read_text())
schema = json.loads((out / 'native-schema-required.json').read_text())
prepared = json.loads((out / 'prepared-image-required.json').read_text())
approval = json.loads((out / 'authorization-required.json').read_text())
assert identity['parent_verified'] is False and identity['ci_run'] is None
assert schema['parent_verified'] is False
assert schema['capability_candidate_manifest_sha256'] == '75fc0de8baa1a8573b54a9afb5f4fe1cad5e557d267b3119483680d970c6a415'
assert schema['capability_format_manifest_sha256'] == 'efb352b1072826ad066461750ca7b87709106499690ca2ec00711e2d38d05427'
assert len(schema['capability_source_pins']) == 8
assert len(schema['capability_generated_source_pins']) == 5
assert len(schema['capability_required_markers']) == 9
assert schema['capability_receipt_parser_sha256'] == hashlib.sha256((out/'parse-capability.py').read_bytes()).hexdigest()
# Exercise every source/schema check with only an in-memory review gate.
# The actual on-disk gate remains false throughout this offline test.
from unittest.mock import patch
original_read_text = Path.read_text
def schema_review_only(path, *args, **kwargs):
    if path == out/'native-schema-required.json':
        return json.dumps(dict(schema, parent_verified=True))
    return original_read_text(path, *args, **kwargs)
with patch.object(Path, 'read_text', schema_review_only):
    assert verify.load_schema()['parent_verified'] is True
assert json.loads((out/'native-schema-required.json').read_text())['parent_verified'] is False
assert prepared['parent_verified'] is False and prepared['image_sha256'] is None
assert approval['human_exact_image_authorized'] is False
assert approval['rf_calibration_authorized'] is False
assert approval['conditional_recovery_authorized'] is False
assert approval['scope'] == 'boot-with-wifi-capability-format-calibration'
assert approval['image_sha256'] is None
assert verify.request_metadata()['wifi_capability_query_limit'] == 1
assert verify.request_metadata()['wifi_network_interface_requested'] is False
assert 'fa13be9baa232b26c89994998cd566fa2ec4e0d96748b5420f1c6504ba203be7' in (out / 'run-approved-boot.py').read_text()
assert not any((out / name).exists() for name in ('manifest.json','payload.bin','image-build.log','flash.log','hardware-live','client-hardware.log','client-hardware.pid'))
deps = json.loads((out / 'parser-dependencies.json').read_text())
for item in deps['copied_parser_sources']:
    local = root / item['path']
    old = root / item['executed_stage_path']
    assert hashlib.sha256(local.read_bytes()).hexdigest() == item['sha256']
    assert hashlib.sha256(old.read_bytes()).hexdigest() == item['executed_stage_sha256']
    assert item['sha256'] == item['executed_stage_sha256']
for args in (('--preflight',),):
    result = subprocess.run((sys.executable, str(out/'verify-image.py'), *args), capture_output=True, text=True)
    assert result.returncode != 0
    assert not verify.IMAGE.exists()
result = subprocess.run((sys.executable, str(out/'run-approved-boot.py'), '--authorized-wifi-capability-format-calibration-boot'), capture_output=True, text=True)
assert result.returncode != 0
assert not (out/'flash.log').exists()
result = subprocess.run((sys.executable, str(out/'parse-capability.py')), capture_output=True, text=True)
assert result.returncode != 0 and 'required' in result.stderr
result = subprocess.run((sys.executable, str(out/'test-capability.py')), capture_output=True, text=True)
assert result.returncode == 0, result.stdout + result.stderr
manifest = json.loads((out/'stage-source-pin.json').read_text())
for item in manifest['files']:
    assert hashlib.sha256((root/item['path']).read_bytes()).hexdigest() == item['sha256'], item['path']
print('PASS: capability stage is offline and all execution gates are closed')
