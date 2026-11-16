#!/usr/bin/env python3
"""Install/rollback the Metal probe addon in an explicitly mounted OFFLINE clone.
Never mounts, flashes or contacts a device. Keep the backup/journal if interrupted;
rollback validates the complete owned inventory before changing anything.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import struct

PATHS = {
    'Nord2Metal.bundle': 'System/Library/Extensions/Nord2Metal.bundle',
    'nord2-metal-probe': 'usr/local/libexec/nord2-metal-probe',
    'varying.bc': 'usr/local/share/nord2-metal/varying.bc',
}
JOB = '/System/Library/LaunchDaemons/org.nord2.metal-probe.plist'
STATE = 'private/var/db/nord2-metal'
LAUNCH = 'System/Library/xpc/launchd.plist'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sync(path):
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
    finally:
        os.close(fd)


def checked(root, rel):
    rel = Path(rel)
    if rel.is_absolute() or '..' in rel.parts:
        raise ValueError('invalid managed path')
    path = root
    for part in rel.parts:
        path /= part
        if path.is_symlink():
            raise ValueError('symlink in managed path: ' + str(rel))
    return path


def inventory(path):
    if path.is_symlink():
        raise ValueError('symlink payload/installation')
    entries = sorted(path.rglob('*')) if path.is_dir() else [path]
    result = {}
    for entry in entries:
        if entry.is_symlink() or (not entry.is_file() and not entry.is_dir()):
            raise ValueError('non-regular addon entry')
        result[str(entry.relative_to(path)) if path.is_dir() else '.'] = (
            sha(entry) if entry.is_file() else 'directory')
    return result


def atomic(path, data, mode=0o644):
    temp = path.with_name(path.name + '.nord2-new')
    # An interrupted previous replacement requires manual review, not overwrite.
    with temp.open('xb') as stream:
        stream.write(data)
        stream.flush()
        os.fchmod(stream.fileno(), mode)
        os.fsync(stream.fileno())
    temp.replace(path)
    sync(path.parent)


def journal(state, record):
    atomic(state / 'receipt.json', (json.dumps(record, indent=2) + '\n').encode())


def apply(root, payload):
    launch = checked(root, LAUNCH)
    original = launch.read_bytes()
    config = plistlib.loads(original)
    if not isinstance(config.get('LaunchDaemons'), dict) or JOB in config['LaunchDaemons']:
        raise ValueError('unexpected launchd database or existing probe job')
    state = checked(root, STATE)
    if state.exists():
        raise ValueError('addon journal already exists; review before reinstall')
    expected = {}
    for name, rel in PATHS.items():
        if checked(root, rel).exists():
            raise ValueError('refuse to replace an existing addon path')
        source = checked(payload, name)
        if not source.exists():
            raise ValueError('missing payload: ' + name)
        expected[rel] = inventory(source)
    state.mkdir(parents=True)
    atomic(state / 'launchd.original', original)
    record = {'version': 2, 'original_launchd_sha256': sha(state / 'launchd.original'),
              'inventory': expected, 'created': [], 'phase': 'preparing'}
    journal(state, record)
    try:
        for name, rel in PATHS.items():
            source, target = payload / name, checked(root, rel)
            target.parent.mkdir(parents=True, exist_ok=True)
            record['created'].append(rel)
            journal(state, record)
            if source.is_dir():
                shutil.copytree(source, target)
            else:
                shutil.copyfile(source, target)
            if inventory(target) != expected[rel]:
                raise ValueError('payload copy verification failed')
            for entry in sorted(target.rglob('*')) if target.is_dir() else [target]:
                if entry.is_file():
                    entry.chmod(0o755 if entry.name in ('Nord2Metal', 'nord2-metal-probe') else 0o644)
                    sync(entry)
                else:
                    sync(entry)
            sync(target.parent)
        config['LaunchDaemons'][JOB] = {
            'Label': 'org.nord2.metal-probe',
            'ProgramArguments': ['/usr/local/libexec/nord2-metal-probe',
                                 '/System/Library/Extensions/Nord2Metal.bundle',
                                 '/usr/local/share/nord2-metal/varying.bc'],
            'RunAtLoad': True, 'UserName': 'root',
            'StandardOutPath': '/private/var/db/nord2-metal/probe.log',
            'StandardErrorPath': '/private/var/db/nord2-metal/probe.log',
        }
        current = plistlib.dumps(config, fmt=plistlib.FMT_BINARY, sort_keys=False)
        record['installed_launchd_sha256'] = hashlib.sha256(current).hexdigest()
        record['phase'] = 'commit-ready'
        journal(state, record)
        atomic(launch, current, launch.stat().st_mode & 0o777)
        record['phase'] = 'committed'
        journal(state, record)
    except Exception:
        # Never silently overwrite unexpected external edits during recovery.
        if sha(launch) in (record['original_launchd_sha256'], record.get('installed_launchd_sha256')):
            if launch.read_bytes() != original:
                atomic(launch, original, launch.stat().st_mode & 0o777)
        record['phase'] = 'interrupted'
        journal(state, record)
        raise
    print('Offline addon installed; existing UI jobs preserved; probe not executed')


def rollback(root):
    state, launch = checked(root, STATE), checked(root, LAUNCH)
    record = json.loads((state / 'receipt.json').read_text())
    if record.get('version') != 2 or set(record['inventory']) != set(PATHS.values()):
        raise ValueError('unexpected receipt')
    if sha(launch) not in (record['original_launchd_sha256'], record.get('installed_launchd_sha256')):
        raise ValueError('launchd has unrelated changes; refuse overwrite')
    if sha(state / 'launchd.original') != record['original_launchd_sha256']:
        raise ValueError('original backup changed')
    # Check everything before any writes, including unrecorded new files/dirs.
    for rel in record['created']:
        if rel not in PATHS.values():
            raise ValueError('invalid created path')
        path = checked(root, rel)
        if not path.exists():
            if record['phase'] in ('interrupted', 'rolled-back'):
                continue
            raise ValueError('addon file missing')
        actual, expected = inventory(path), record['inventory'][rel]
        if record['phase'] == 'interrupted':
            # A partial copy may only contain byte-identical intended files.
            if any(key not in expected or expected[key] != value for key, value in actual.items()):
                raise ValueError('partial addon changed; manual review required')
        elif actual != expected:
            raise ValueError('addon inventory changed; refuse deletion')
    atomic(launch, (state / 'launchd.original').read_bytes(), launch.stat().st_mode & 0o777)
    for rel in record['created']:
        path = checked(root, rel)
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
        sync(path.parent)
    record['phase'] = 'rolled-back'
    journal(state, record)
    print('Original launchd restored byte for byte; verified addon files removed')


def validate_payload(payload):
    record=json.loads((payload/'manifest.json').read_text())
    actual={}
    for name in PATHS:
        item=checked(payload,name)
        for key,value in inventory(item).items():
            if value!='directory':actual[name if key=='.' else name+'/'+key]=value
    if record.get('version')!=1 or record.get('files')!=actual:
        raise ValueError('addon payload differs from manifest')
    for name,kind in [('Nord2Metal.bundle/Nord2Metal',8),('nord2-metal-probe',2)]:
        if struct.unpack('<4I',(payload/name).read_bytes()[:16])!=(0xfeedfacf,0x100000c,0,kind):
            raise ValueError('unexpected native Mach-O '+name)
    info=plistlib.loads((payload/'Nord2Metal.bundle/Info.plist').read_bytes())
    if info.get('CFBundleExecutable')!='Nord2Metal' or info.get('NSPrincipalClass')!='TeguComputeDevice':
        raise ValueError('unexpected Metal plugin identity')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=['apply', 'rollback'])
    parser.add_argument('root', type=Path)
    parser.add_argument('--payload', type=Path)
    args = parser.parse_args()
    if os.geteuid()!=0:
        parser.error('run inside the offline guest as root to preserve iOS ownership')
    root = args.root.resolve()
    if root == Path('/') or not root.is_mount():
        parser.error('root must be an explicitly mounted offline clone, never /')
    if args.operation == 'apply':
        if not args.payload:
            parser.error('--payload required')
        validate_payload(args.payload.resolve())
        apply(root, args.payload.resolve())
    else:
        rollback(root)
