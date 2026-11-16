#!/usr/bin/env python3
"""Offline addon tests against realistic monolithic launchd fixtures."""
from pathlib import Path
import importlib.util
import tempfile
import plistlib
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('install', Path(__file__).parents[1] / 'tools/stage_metal_root.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

def fixture(base):
    root, payload = base / 'root', base / 'payload'
    launch = root / m.LAUNCH
    launch.parent.mkdir(parents=True)
    config = {'LaunchDaemons': {'backboardd': {'Program': '/usr/libexec/backboardd'},
                               'mediaserverd': {'Program': '/usr/sbin/mediaserverd'},
                               'wifid': {'Program': '/usr/sbin/wifid'}}, 'unrelated': b'preserve'}
    original = plistlib.dumps(config)
    launch.write_bytes(original)
    (payload / 'Nord2Metal.bundle').mkdir(parents=True)
    (payload / 'Nord2Metal.bundle/Nord2Metal').write_bytes(b'native binary')
    (payload / 'Nord2Metal.bundle/Info.plist').write_bytes(plistlib.dumps({'name': 'Nord2Metal'}))
    (payload / 'nord2-metal-probe').write_bytes(b'probe')
    (payload / 'varying.bc').write_bytes(b'bitcode')
    return root, payload, original, config

def refusal(call):
    try: call()
    except ValueError: return
    raise AssertionError('unsafe operation accepted')

with tempfile.TemporaryDirectory() as d:
    base = Path(d)
    r,p,original,config = fixture(base / 'ok')
    m.apply(r,p)
    installed = plistlib.loads((r/m.LAUNCH).read_bytes())
    assert installed['LaunchDaemons'].pop(m.JOB)['RunAtLoad']
    assert installed == config
    m.rollback(r)
    assert (r/m.LAUNCH).read_bytes() == original
    assert not any((r/path).exists() for path in m.PATHS.values())
    m.rollback(r)  # safe repeated recovery
    for failure in ['launchd','file','extra','symlink']:
        r,p,original,_ = fixture(base / failure)
        m.apply(r,p)
        bundle = r/m.PATHS['Nord2Metal.bundle']
        if failure == 'launchd': (r/m.LAUNCH).write_bytes(b'external launchd change')
        if failure == 'file': (bundle/'Nord2Metal').write_bytes(b'external file change')
        if failure == 'extra': (bundle/'unrelated').write_bytes(b'do not delete')
        if failure == 'symlink': (bundle/'external').symlink_to('/etc/passwd')
        before = (r/m.LAUNCH).read_bytes()
        refusal(lambda: m.rollback(r))
        assert (r/m.LAUNCH).read_bytes() == before and bundle.exists()
    r,p,original,_ = fixture(base / 'payload-link')
    (p/'Nord2Metal.bundle/external').symlink_to('/etc/passwd')
    refusal(lambda: m.apply(r,p))
    assert not (r/m.STATE).exists() and (r/m.LAUNCH).read_bytes() == original
    r,p,original,_ = fixture(base / 'collision')
    target = r/m.PATHS['nord2-metal-probe'];target.parent.mkdir(parents=True);target.write_bytes(b'keep')
    refusal(lambda: m.apply(r,p))
    assert not (r/m.STATE).exists() and target.read_bytes() == b'keep'
    r,p,original,_ = fixture(base / 'interrupt')
    real_copy = m.shutil.copyfile
    def broken_copy(src,dst,*args,**kwargs):
        if str(src).endswith('/nord2-metal-probe'): raise OSError('injected disk failure')
        return real_copy(src,dst,*args,**kwargs)
    try:
        with patch.object(m.shutil,'copyfile',broken_copy): m.apply(r,p)
    except OSError: pass
    else: raise AssertionError('fault not injected')
    assert (r/m.LAUNCH).read_bytes() == original
    m.rollback(r)
    assert not any((r/path).exists() for path in m.PATHS.values())
print('PASS: addon install, exact rollback, preserved UI jobs, 6 refusal/recovery scenarios')
