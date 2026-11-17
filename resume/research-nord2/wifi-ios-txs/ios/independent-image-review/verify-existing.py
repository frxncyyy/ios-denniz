#!/usr/bin/env python3
"""Independent existing-image checks only; never call builder/runner/device tools."""
from pathlib import Path
import argparse, hashlib, importlib.util, json, re, resource, struct, subprocess, sys
sys.dont_write_bytecode = True
P = Path(__file__).resolve().parent
S = P.parent
R = next(p for p in S.parents if (p / 'AGENTS.md').exists())
IMAGE = 'shim/out/nord2-xnu/nord2-ios-wifi-txs-timing-stop23.img'
PRIMARY = ('shim/out/nord2-xnu/nord2-ios-wifi-identity-keychain-stop23.img', 33462272,
           'd9a44316f6b257221a948a32483a6778b33cdf5057a3f79ffecfceebf0318de3')
SECONDARY = ('shim/out/nord2-xnu/nord2-ios-wifi-security-battery-stop23.img', 33462272,
             '6e4ce860cfbd0b27fbace65e61e96edc8d2af8067603652cd67a0939c53331c6')
RESCUE = ('research/nord2/rescue/nord2-adb-rescue.img', 28884992,
          'd78bf9fe720a1d293b863ea0a8b30e0df4b5eafb097b1343a3c30e7c3d03bac5')


def sha(p):
    with p.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def load(p):
    return json.loads(p.read_text())


def save(p, v):
    p.write_text(json.dumps(v, indent=2) + '\n')


def extent(payload):
    assert len(payload) >= 0x2a8 and payload[:8] == b'SWEETXNU'
    offset, size = struct.unpack_from('<QQ', payload, 0x20)
    assert offset <= len(payload) and size <= len(payload) - offset
    return payload[offset:offset + size]


def verify_file(triple):
    name, size, digest = triple
    p = R / name
    assert p.stat().st_size == size and sha(p) == digest, name
    return {'path': name, 'bytes': size, 'sha256': digest}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--execute', action='store_true', help='Only after root confirms completed image')
    ap.add_argument('--kernel-sha256', help='Independent root-notified artifact SHA256')
    ap.add_argument('--ci-run', type=int, help='Root-notified exact completed CI')
    ap.add_argument('--ci-attempt', type=int, help='Root-notified exact successful attempt')
    ap.add_argument('--source-commit', help='Root-notified exact40-byte hexadecimal sourceSHA')
    ap.add_argument('--kernel-bytes', type=int, help='Root-notified exact artifact length')
    args = ap.parse_args()
    CI, SOURCE = args.ci_run, args.source_commit
    if not args.execute:
        save(P / 'plan.json', {
            'state': 'PREPARED_ONLY_IMAGE_NOT_REVIEWED', 'ci_run': CI, 'expected_source': SOURCE,
            'image_path': IMAGE, 'address_space_limit_bytes': 1 << 30,
            'primary_recovery': PRIMARY, 'secondary_recovery': SECONDARY, 'rescue': RESCUE,
            'checks': ['Exact completed CI/source/kernel and artifact SHA256SUMS',
                       'Existing-image verifier --check-existing only; preserve builder receipts',
                       'Android boot capacity/header ID/loader/Linux/DT/rescue/payload segment byte checks',
                       'Raw chosen MAC flags0 and six bytes equal pinned calibration; no bytes printed',
                       'ADT byte-exact previous d9a identity payload; remove identity gives executed6e4 ADT',
                       'Source/schema/gate pins and no used runner; artifacts unchanged after check'],
            'new_image_verified': False, 'builder_or_runner_invoked': False, 'device_access': False,
        })
        print('PREPARED only; waiting for explicit completed-image notice')
        return
    assert args.kernel_sha256 and re.fullmatch(r'[0-9a-f]{64}', args.kernel_sha256)
    assert CI and CI > 0 and args.ci_attempt and args.ci_attempt > 0
    assert SOURCE and re.fullmatch(r'[0-9a-f]{40}', SOURCE)
    assert args.kernel_bytes and 0 < args.kernel_bytes < 33554432
    assert not (P / 'report.json').exists(), 'Preserve existing independent review'
    for name in ['manifest.json', 'payload.bin', 'image-build.log']:
        assert (S / name).is_file(), 'builder pending: ' + name
    assert 'FAILURES: none' in (S / 'image-build.log').read_text()
    identity = load(S / 'identity-required.json')
    schema = load(S / 'native-schema-required.json')
    assert identity['parent_verified'] and schema['parent_verified'] and not schema['schema_pending']
    assert identity['ci_run'] == CI and identity['source_commit'] == SOURCE
    assert identity['kernel_sha256'] == args.kernel_sha256
    ci_path = R / identity['ci_run_json']
    ci = load(ci_path)
    assert (ci['databaseId'], ci['attempt'], ci['status'], ci['conclusion'], ci['headSha']) == (
        CI, args.ci_attempt, 'completed', 'success', SOURCE)
    if 'ci_attempt' in identity:
        assert identity['ci_attempt'] == args.ci_attempt
    if 'kernel_bytes' in identity:
        assert identity['kernel_bytes'] == args.kernel_bytes
    protected = ['verify-image.py', 'identity-required.json', 'native-schema-required.json',
                 'prepared-image-required.json', 'authorization-required.json', 'baseline.json',
                 'recovery-image.json', 'manifest.json', 'payload.bin', 'image-build.log',
                 'build-image.sh', 'run-approved-boot.py', 'parser-dependency.json']
    protected += [n for n in ['source-proposals.json', 'source-manifest.json', 'stage-files-manifest.json'] if (S / n).is_file()]
    stage_pins = {n: sha(S / n) for n in protected}
    assert not (S / 'RUNNER_EXECUTION_USED').exists()
    prepared = load(S / 'prepared-image-required.json')
    approval = load(S / 'authorization-required.json')
    assert not prepared['parent_verified']
    assert all(approval[k] is False for k in ['human_exact_image_authorized',
                                            'rf_calibration_authorized', 'conditional_recovery_authorized'])
    assert approval['image_sha256'] is None and approval['device_serial'] is None
    resource.setrlimit(resource.RLIMIT_AS, (1 << 30, 1 << 30))
    with (P / 'verify-existing.log').open('x') as log:
        result = subprocess.run([sys.executable, '-B', str(S / 'verify-image.py'), '--check-existing'],
                                cwd=R, stdout=log, stderr=subprocess.STDOUT)
    assert result.returncode == 0, 'Existing-image verifier failed; see private log'
    assert stage_pins == {n: sha(S / n) for n in protected}, 'Stage/gates changed during verification'
    manifest = load(S / 'manifest.json')
    image = R / manifest['image']
    kernel = R / identity['kernel_path']
    assert manifest['image'] == IMAGE and image.stat().st_size == manifest['image_bytes']
    assert sha(image) == manifest['image_sha256']
    assert kernel.stat().st_size == args.kernel_bytes and sha(kernel) == args.kernel_sha256
    assert manifest['kernel_sha256'] == args.kernel_sha256
    assert manifest['ci_run'] == CI and manifest['expected_kernel_source'] == SOURCE
    assert manifest['kernel_source_identity_verified'] and manifest['kernel_payload_segments_byte_verified']
    assert 0 < image.stat().st_size < 33554432
    assert manifest['boot_spare_bytes'] == 33554432 - image.stat().st_size
    assert manifest['hardware_tested'] is False and manifest['private_factory_identity_adt_verified']
    for flag in ['wifi_rejoin_supported', 'wifi_roaming_supported', 'wifi_sae_supported', 'wifi_required_pmf_supported', 'wifi_private_address_supported', 'wifi_keychain_encryption_claim', 'wifi_keychain_secure_persistence_claim']:
        assert manifest[flag] is False
    for flag in ['wifi_first_connection_only', 'wifi_preboot_factory_identity_requested', 'wifi_insecure_keychain_compatibility_requested']:
        assert manifest[flag] is True
    sums = (kernel.parent / 'SHA256SUMS').read_text().splitlines()
    matches = [x.split()[0] for x in sums if len(x.split()) == 2 and
               Path(x.split()[1].lstrip('*')).name == kernel.name]
    assert matches == [args.kernel_sha256]
    payload = (S / 'payload.bin').read_bytes()
    assert hashlib.sha256(payload).hexdigest() == manifest['payload_sha256']
    actual = extent(payload)
    sys.path.insert(0, str(R / 'tools'))
    import adt
    tree, used = adt.parse(actual)
    assert used == len(actual) and adt.serialize(tree) == actual
    chosen = [n for n in tree.children if n.name == 'chosen']
    assert len(chosen) == 1
    cal = (R / 'research/nord2/wifi/calibration/WIFI').read_bytes()
    assert len(cal) == 6146 and hashlib.sha256(cal).hexdigest() == '0000000000000000000000000000000000000000000000000000000000000000'
    assert [(f, v) for k, f, v in chosen[0].props if k == 'nord2-wifi-calibration'] == [(0, cal)]
    assert [(f, v) for k, f, v in chosen[0].props if k == 'mac-address-wifi0'] == [(0, cal[4:10])]
    assert any(cal[4:10]) and not cal[4] & 1
    # Identity is unchanged from the actually executed d9a image, whose immutable
    # existing payload+manifest are read only and never rewritten by this check.
    previous = R / 'research/nord2/wifi-ios-identity/ios'
    prior_manifest = load(previous / 'manifest.json')
    assert prior_manifest['image_sha256'] == PRIMARY[2]
    assert sha(previous / 'payload.bin') == prior_manifest['payload_sha256']
    assert actual == extent((previous / 'payload.bin').read_bytes())
    chosen[0].props = [x for x in chosen[0].props if x[0] != 'mac-address-wifi0']
    baseline = load(S / 'baseline.json')
    old = R / baseline['identity_baseline_payload_path']
    assert sha(old) == baseline['identity_baseline_payload_sha256']
    old_adt = extent(old.read_bytes())
    assert len(old_adt) == baseline['identity_baseline_adt_bytes'] == 19160
    assert hashlib.sha256(old_adt).hexdigest() == baseline['identity_baseline_adt_sha256']
    assert adt.serialize(tree) == old_adt
    recovery = load(S / 'recovery-image.json')
    assert (recovery['path'], recovery['bytes'], recovery['sha256']) == PRIMARY
    assert baseline['fallback_image_sha256'] == PRIMARY[2] and baseline['fallback_image_bytes'] == PRIMARY[1]
    secondary = baseline['secondary_recovery_image']
    assert (secondary['path'], secondary['bytes'], secondary['sha256']) == SECONDARY
    assert baseline['ram_rescue_sha256'] == RESCUE[2]
    recovered = [verify_file(x) for x in [PRIMARY, SECONDARY, RESCUE]]
    assert not (S / 'RUNNER_EXECUTION_USED').exists()
    assert stage_pins == {n: sha(S / n) for n in protected}
    report = {
        'verdict': 'PASS_EXISTING_IMAGE_OFFLINE_ONLY', 'image': IMAGE,
        'image_bytes': image.stat().st_size, 'image_sha256': manifest['image_sha256'],
        'boot_capacity_bytes': 33554432, 'boot_spare_bytes': manifest['boot_spare_bytes'],
        'payload_sha256': manifest['payload_sha256'], 'kernel_sha256': args.kernel_sha256,
        'kernel_bytes': args.kernel_bytes, 'source_commit': SOURCE, 'ci_run': CI,
        'attempt': ci['attempt'], 'ci_run_json_sha256': sha(ci_path),
        'raw_chosen6_flags0_equals_calibration': True, 'adt_exact_executed_d9a': True,
        'remove_only_identity_recovers_executed6e4_adt': True, 'recovery_images': recovered,
        'source_schema_identity_gates_verified': True, 'prepared_gate_during_review': False,
        'human_RF_recovery_approval_during_review': False, 'runner_unused': True,
        'stage_artifacts_and_gates_unchanged': True, 'stage_pins': stage_pins,
        'native_registry_source_sha256': sha(R / 'shim/nord2-xnu/wifi_ios_registry.cpp'),
        'identity_tool_sha256': sha(R / 'tools/nord2-wifi-identity.py'),
        'address_space_limit_bytes': 1 << 30,
        'verifier_children_max_rss_kib': resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,
        'device_access': False, 'builder_or_runner_invoked': False,
        'limitations': ['Offline artifact/header/segment/ADT checks; no hardware association/key/data/IP claim.',
                        'Actual native owner equality is source/linked-reviewed, not a new live observation.',
                        'No raw address/calibration/credential bytes printed.'],
    }
    save(P / 'report.json', report)
    print(json.dumps({k: report[k] for k in ['verdict', 'image_bytes', 'image_sha256',
                                           'boot_spare_bytes', 'source_commit', 'kernel_sha256']}, indent=2))


if __name__ == '__main__':
    main()
