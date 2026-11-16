#!/usr/bin/env python3
"""Install the discovery probe into a mounted test-root clone (run as guest root).

This edits the consolidated iOS launchd job table so the probe runs once at boot.
It does not flash a phone or implement a Metal device.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct

LABEL = 'org.tegu.metal-discovery'
PROGRAM = '/usr/local/bin/tegu-metal-probe'
# iOS launchd's embedded Bootstrap/Paths includes System/Library, not /Library.
JOB = '/System/Library/LaunchDaemons/' + LABEL + '.plist'
OFFSCREEN_MANIFEST = 'var/db/tegu-compositor-offscreen-ui.json'
OFFSCREEN_UI_LABELS = ('com.apple.backboardd', 'com.apple.SpringBoard')


def install(root, binary, bundle=None, probe_arguments=(), offscreen_ui=False,
            null_tee_stdio=False):
    if any(not isinstance(arg, str) or "\0" in arg for arg in probe_arguments):
        raise ValueError('Probe arguments must be NUL-free strings')
    root = root.resolve()
    if root == Path('/'):
        raise ValueError('Pass a mounted test-root clone, not the running root')
    header = binary.read_bytes()[:16]
    if len(header) != 16 or struct.unpack('<4I', header) != (0xfeedfacf, 0x100000c, 0, 2):
        raise ValueError('Expected arm64 Mach-O executable')
    bundle_target = root / 'System/Library/Extensions/TeguMetal.bundle'
    if bundle is not None:
        info = plistlib.loads((bundle / 'Info.plist').read_bytes())
        if info.get('CFBundleExecutable') != 'TeguMetal' or info.get('NSPrincipalClass') != 'TeguComputeDevice':
            raise ValueError('Unexpected Metal plugin metadata')
        header = (bundle / 'TeguMetal').read_bytes()[:16]
        if struct.unpack('<4I', header) != (0xfeedfacf, 0x100000c, 0, 8):
            raise ValueError('Expected arm64 Mach-O bundle')
        if bundle_target.exists() or any(p.is_symlink() for p in bundle.rglob('*')):
            raise ValueError('Existing plugin or symlink in bundle')
    config = root / 'System/Library/xpc/launchd.plist'
    original = config.read_bytes()
    table = plistlib.loads(original)
    jobs = table['LaunchDaemons']
    offscreen_before = {}
    if offscreen_ui:
        for label in OFFSCREEN_UI_LABELS:
            matches = [(key, job) for key, job in jobs.items() if job.get('Label') == label]
            if len(matches) != 1:
                raise ValueError(f'Expected exactly one LaunchDaemons job with label {label}')
            key, job = matches[0]
            original_job = copy.deepcopy(job)
            offscreen_before[label] = {
                'plist_key': key,
                'original_job_sha256': hashlib.sha256(plistlib.dumps(
                    original_job, fmt=plistlib.FMT_BINARY, sort_keys=True)).hexdigest(),
                'original_disabled': original_job.get('Disabled', False),
            }
            job['Disabled'] = True
    if JOB in jobs or any(j.get('Label') == LABEL for j in jobs.values()):
        raise ValueError('Probe already registered; refusing duplicate job')
    target = root / PROGRAM.lstrip('/')
    if target.exists():
        raise ValueError('Probe destination already exists')
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.parent.resolve().is_relative_to(root):
        raise ValueError('Probe directory escapes the test root')
    jobs[JOB] = {
        'Label': LABEL, 'ProgramArguments': [PROGRAM, *probe_arguments], 'RunAtLoad': True,
        'UserName': 'root',
        'StandardOutPath': '/dev/null' if null_tee_stdio else '/dev/console',
        'StandardErrorPath': '/dev/null' if null_tee_stdio else '/dev/console',
    }
    # No KeepAlive: a missing Metal device must not create a respawn loop.
    encoded = plistlib.dumps(table, fmt=plistlib.FMT_BINARY, sort_keys=False)
    manifest_path = root / OFFSCREEN_MANIFEST
    manifest_data = None
    if offscreen_ui:
        if manifest_path.exists():
            raise ValueError('Offscreen UI manifest already exists')
        manifest = {
            'schema': 1,
            'mode': 'offscreen-ui-diagnostic',
            'source_launchd_sha256': hashlib.sha256(original).hexdigest(),
            'installed_launchd_sha256': hashlib.sha256(encoded).hexdigest(),
            'disabled_jobs': {
                label: {**entry, 'installed_job_sha256': hashlib.sha256(plistlib.dumps(
                    jobs[entry['plist_key']], fmt=plistlib.FMT_BINARY, sort_keys=True)).hexdigest()}
                for label, entry in offscreen_before.items()
            },
        }
        manifest_data = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    metadata = config.stat()
    temp = config.with_name(config.name + '.tegu-metal-new')
    backup = config.with_name(config.name + '.tegu-metal-original')
    if temp.exists() or backup.exists():
        raise ValueError('Existing installer transaction files')
    shutil.copyfile(binary, target)
    os.chmod(target, 0o755)
    os.chown(target, 0, 0)
    if bundle is not None:
        bundle_target.parent.mkdir(parents=True, exist_ok=True)
        if not bundle_target.parent.resolve().is_relative_to(root):
            raise ValueError('Plugin directory escapes test root')
        shutil.copytree(bundle, bundle_target)
        for path in (bundle_target, *bundle_target.rglob('*')):
            os.chown(path, 0, 0)
            os.chmod(path, 0o755 if path.is_dir() or path.name == 'TeguMetal' else 0o644)
    backup.write_bytes(original)
    os.chmod(backup, metadata.st_mode & 0o7777)
    os.chown(backup, metadata.st_uid, metadata.st_gid)
    temp.write_bytes(encoded)
    os.chmod(temp, metadata.st_mode & 0o7777)
    os.chown(temp, metadata.st_uid, metadata.st_gid)
    if offscreen_ui:
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        if not manifest_path.parent.resolve().is_relative_to(root):
            raise ValueError('Offscreen manifest escapes the test root')
        manifest_path.write_bytes(manifest_data)
        os.chmod(manifest_path, 0o644)
        os.chown(manifest_path, 0, 0)
    temp.replace(config)
    assert plistlib.loads(config.read_bytes())['LaunchDaemons'][JOB] == jobs[JOB]
    assert target.read_bytes() == binary.read_bytes()
    if null_tee_stdio:
        print('TEGU_METAL_PROBE_INSTALLED: one boot invocation, stdio=/dev/null SWEET tee requested; not device-tested')
    else:
        print('TEGU_METAL_PROBE_INSTALLED: one boot invocation, console output; not device-tested')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    parser.add_argument('binary', type=Path)
    parser.add_argument('--bundle', type=Path, help='Optional unregistered TeguMetal.bundle')
    parser.add_argument('--offscreen-ui', action='store_true',
                        help='Diagnostic clone only: disable the exact backboardd and SpringBoard launchd jobs')
    parser.add_argument('--null-tee-stdio', action='store_true',
                        help='Diagnostic clone only: direct this probe job stdout/stderr to /dev/null (SWEET tee is required for console capture)')
    arguments = parser.add_mutually_exclusive_group()
    arguments.add_argument('--probe-argument', action='append', default=None,
                           help='One literal launchd argv element (repeatable; no shell parsing)')
    arguments.add_argument('--probe-arguments-json', type=Path,
                           help='JSON file containing an argv string array')
    args = parser.parse_args()
    if args.probe_arguments_json:
        values = json.loads(args.probe_arguments_json.read_text())
        if not isinstance(values, list) or any(not isinstance(x, str) for x in values):
            parser.error('--probe-arguments-json must contain a JSON string array')
    else:
        values = args.probe_argument or []
    install(args.root, args.binary, args.bundle, values, args.offscreen_ui, args.null_tee_stdio)
