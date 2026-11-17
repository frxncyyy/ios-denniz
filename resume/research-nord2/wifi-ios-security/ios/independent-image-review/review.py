#!/usr/bin/env python3
"""Read-only EXISTING-image review. Run only after root confirms builder completion."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import resource
import subprocess
import sys

E = Path(__file__).resolve().parent
S = E.parent
R = next(p for p in S.parents if (p / 'AGENTS.md').is_file())
LIMIT = 1073741824
IMAGE = 'shim/out/nord2-xnu/nord2-ios-wifi-security-battery-stop23.img'
IDENTITY = dict(ci_run=39790703398, ci_attempt=1,
                source_commit='938cb89b827bf7cd477044714ed23b935d4d6bd0',
                kernel_sha256='86b44266ff4a20d24ae1bada1eda0533bc7f8a6e75634d940ba5d5453040ea2e',
                kernel_bytes=17703096)
META = ['verify-image.py', 'build-image.sh', 'run-approved-boot.py',
        'identity-required.json', 'native-schema-required.json',
        'prepared-image-required.json', 'authorization-required.json',
        'recovery-image.json', 'parser-dependency.json']

def h(p):
    x = hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(1048576), b''):
            x.update(b)
    return x.hexdigest()

def read(p):
    return json.loads(p.read_text())

def save(p, x):
    with p.open('x') as f:
        f.write(json.dumps(x, indent=2) + '\n')

def metadata():
    identity = read(S / 'identity-required.json')
    assert identity['parent_verified'] is True
    assert all(identity[k] == v for k, v in IDENTITY.items())
    schema = read(S / 'native-schema-required.json')
    assert schema['parent_verified'] is True and schema['schema_pending'] is False
    assert len(schema['connection_diagnostics']) == len(schema['linked_reviews']) == 6
    pins = {}
    for key in ['connection_diagnostics', 'linked_reviews']:
        for p in schema[key]:
            path = Path(p['path'])
            assert not path.is_absolute() and '..' not in path.parts
            assert re.fullmatch('[0-9a-f]{64}', p['sha256'])
            assert h(R / path) == p['sha256'], path
            assert p['path'] not in pins
            pins[p['path']] = p['sha256']
    prepared = read(S / 'prepared-image-required.json')
    assert prepared['parent_verified'] is False
    assert prepared['image_path'] == IMAGE
    assert prepared['image_bytes'] is None and prepared['image_sha256'] is None
    auth = read(S / 'authorization-required.json')
    for k in ['human_exact_image_authorized', 'rf_calibration_authorized',
              'conditional_recovery_authorized']:
        assert auth[k] is False
    assert auth['image_sha256'] is None and auth['device_serial'] is None
    assert auth['scope'] == 'boot-with-wifi-ios-security'
    assert not (S / 'RUNNER_EXECUTION_USED').exists()
    recovery = read(S / 'recovery-image.json')
    assert recovery['path'] == 'shim/out/nord2-xnu/nord2-ios-wifi-discovery-battery-stop23.img'
    assert recovery['sha256'] == '52d4b1591b3b8d906214ec37bc93e309d5a3b69d59fd8a013c424d755a504aca'
    assert recovery['bytes'] == 33415168
    return dict(metadata_pins={n: h(S / n) for n in META},
                diagnostic_and_linked_pins=pins, identity=identity,
                connection_diagnostics=schema['connection_diagnostics'],
                linked_reviews=schema['linked_reviews'])

def limit_child():
    resource.setrlimit(resource.RLIMIT_AS, (LIMIT, LIMIT))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--prepare', action='store_true', help='pin metadata only; no image/payload reads')
    ap.add_argument('--builder-completed', action='store_true')
    ap.add_argument('--image-bytes', type=int)
    ap.add_argument('--image-sha256')
    ap.add_argument('--payload-sha256')
    ap.add_argument('--spare-bytes', type=int)
    args = ap.parse_args()
    now = metadata()
    if args.prepare:
        assert not args.builder_completed
        save(E / 'plan.json', dict(mode='WAIT_FOR_ROOT_COMPLETED_IMAGE_IDENTITY',
             image=IMAGE, address_space_limit_bytes=LIMIT,
             image_or_payload_read=False, verifier_invoked=False, **now))
        print('Metadata pinned only; image, payload and builder output were not read.')
        return
    assert args.builder_completed, 'Root must first confirm successful completed builder.'
    assert args.image_bytes and 0 < args.image_bytes < 33554432
    assert args.spare_bytes == 33554432 - args.image_bytes
    for digest in [args.image_sha256, args.payload_sha256]:
        assert isinstance(digest, str) and re.fullmatch('[0-9a-f]{64}', digest)
    plan = read(E / 'plan.json')
    assert all(now[k] == plan[k] for k in now), 'Pinned review metadata changed; review explicitly before repinning.'
    for n in ['before.json', 'verify.log', 'report.json', 'manifest.json']:
        assert not (E / n).exists(), 'Preserve prior independent evidence: ' + n
    image = R / IMAGE
    assert image.stat().st_size == args.image_bytes and h(image) == args.image_sha256
    assert h(S / 'payload.bin') == args.payload_sha256
    run = read(R / now['identity']['ci_run_json'])
    assert run['databaseId'] == IDENTITY['ci_run'] and run['attempt'] == IDENTITY['ci_attempt']
    assert run['status'] == 'completed' and run['conclusion'] == 'success'
    assert run['headSha'] == IDENTITY['source_commit']
    kernel = R / now['identity']['kernel_path']
    assert kernel.stat().st_size == IDENTITY['kernel_bytes'] and h(kernel) == IDENTITY['kernel_sha256']
    recovery = read(S / 'recovery-image.json')
    assert (R / recovery['path']).stat().st_size == recovery['bytes']
    assert h(R / recovery['path']) == recovery['sha256']
    tracked = META + ['payload.bin', 'manifest.json', 'image-build.log']
    before = dict(pins={n: h(S / n) for n in tracked},
                  image_sha256=args.image_sha256, image_bytes=args.image_bytes,
                  root_supplied_builder_completed=True, **now)
    save(E / 'before.json', before)
    command = [sys.executable, str(S / 'verify-image.py'), '--check-existing']
    with (E / 'verify.log').open('x') as log:
        result = subprocess.run(command, cwd=R, stdout=log, stderr=subprocess.STDOUT,
                                preexec_fn=limit_child)
    assert result.returncode == 0, 'Verifier failed; preserve log and report blocker.'
    receipt = json.loads((E / 'verify.log').read_text().splitlines()[-1])
    assert receipt == dict(existing_prepared_image_verified=True,
                           image_sha256=args.image_sha256,
                           kernel_sha256=IDENTITY['kernel_sha256'])
    assert metadata() == now
    assert all(h(S / n) == digest for n, digest in before['pins'].items())
    assert image.stat().st_size == args.image_bytes and h(image) == args.image_sha256
    manifest = read(S / 'manifest.json')
    assert manifest['image_sha256'] == args.image_sha256 and manifest['image_bytes'] == args.image_bytes
    assert manifest['payload_sha256'] == args.payload_sha256
    assert manifest['kernel_sha256'] == IDENTITY['kernel_sha256']
    assert manifest['boot_spare_bytes'] == args.spare_bytes
    assert len(manifest['commandline'].encode()) == 539
    assert manifest['prepared_only'] is True and manifest['hardware_tested'] is False
    for k in ['wifi_association_requested', 'wifi_wpa2_psk_ccmp_requested',
              'wifi_bsd_ethernet_requested', 'wifi_first_connection_only']:
        assert manifest[k] is True
    for k in ['wifi_sae_supported', 'wifi_required_pmf_supported', 'wifi_private_address_supported']:
        assert manifest[k] is False
    report = dict(verdict='PASS_INDEPENDENT_EXISTING_IMAGE', findings=[],
                  command=command, address_space_limit_bytes=LIMIT, exit_status=0,
                  image=IMAGE, image_bytes=args.image_bytes, image_sha256=args.image_sha256,
                  payload_sha256=args.payload_sha256, boot_spare_bytes=args.spare_bytes,
                  ci=IDENTITY, commandline_bytes=539,
                  prepared_gate_false=True, authorization_flags_all_false=True,
                  runner_unused=True, all_tracked_evidence_unchanged=True,
                  connection_diagnostic_pins_checked=6, linked_review_pins_checked=6,
                  verification=receipt,
                  checks=['Independent loader rebuild and payload byte equality',
                          'Actual kernel segments/entry/fixups and markers',
                          'Private calibration bounds and baseline ADT equality',
                          'Original Linux/DT and rescue byte equality',
                          'Android header ID, exact existing manifest and payload',
                          'Recovery identity and exact CI/source/kernel'],
                  limits=['No hardware, association, key installation, DHCP/IP or Internet proof.',
                          'This report grants no device/image authorization.'])
    save(E / 'report.json', report)
    files = ['review.py', 'plan.json', 'before.json', 'verify.log', 'report.json']
    save(E / 'manifest.json', dict(files={n: h(E / n) for n in files}))
    print(json.dumps(dict(report_sha256=h(E / 'report.json'), manifest_sha256=h(E / 'manifest.json'))))

if __name__ == '__main__':
    main()
