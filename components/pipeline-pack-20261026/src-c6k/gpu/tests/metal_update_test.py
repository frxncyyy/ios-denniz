#!/usr/bin/env python3
"""Transactional update/recovery on fixtures; no device access."""
from pathlib import Path
import sys, tempfile, json, plistlib, struct, hashlib
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import update_metal_root as u
s=u.s

def setup(base):
    root=base/'root';a=base/'a';b=base/'b'
    launch=root/s.LAUNCH;launch.parent.mkdir(parents=True)
    launch.write_bytes(plistlib.dumps({'LaunchDaemons':{'backboardd':{'Program':'/usr/libexec/backboardd'},'wifid':{'Program':'/usr/sbin/wifid'}}}))
    for target,version in [(a,1),(b,2)]:
        (target/'Nord2Metal.bundle').mkdir(parents=True)
        (target/'Nord2Metal.bundle/Nord2Metal').write_bytes(struct.pack('<4I',0xfeedfacf,0x100000c,0,8)+bytes([version])*100)
        (target/'Nord2Metal.bundle/Info.plist').write_bytes(plistlib.dumps({'CFBundleExecutable':'Nord2Metal','NSPrincipalClass':'TeguComputeDevice','test-version':version}))
        (target/'nord2-metal-probe').write_bytes(struct.pack('<4I',0xfeedfacf,0x100000c,0,2)+bytes([version])*300)
        (target/'varying.bc').write_bytes(b'authored-'+bytes([version]))
        files={str(p.relative_to(target)):s.sha(p) for p in target.rglob('*') if p.is_file()}
        (target/'manifest.json').write_text(json.dumps({'version':1,'files':files}))
        s.validate_payload(target)
    s.apply(root,a);return root,a,b

with tempfile.TemporaryDirectory() as temp:
    base=Path(temp)
    r,a,b=setup(base/'normal');before=u.files(r);launch=(r/s.LAUNCH).read_bytes();backup=u.update(r,b)
    assert u.files(r)!=before and (r/s.LAUNCH).read_bytes()==launch
    u.restore(r,backup);assert u.files(r)==before and (r/s.LAUNCH).read_bytes()==launch
    s.rollback(r);assert not any((r/x).exists() for x in s.PATHS.values())
    # A probe-only update must not rewrite or back up the large unchanged
    # bundle/bitcode. Check inode identity as well as content, including undo.
    r,a,b=setup(base/'selective')
    for p in a.rglob('*'):
        if p.is_file() and p.name not in ('nord2-metal-probe','manifest.json'):
            (b/p.relative_to(a)).write_bytes(p.read_bytes())
    (b/'manifest.json').write_text(json.dumps({'version':1,'files':{
        str(p.relative_to(b)):s.sha(p) for p in b.rglob('*') if p.is_file() and p.name!='manifest.json'}}))
    before=u.files(r);changed=s.PATHS['nord2-metal-probe']
    inodes={rel:(r/rel).stat().st_ino for rel in before if rel!=changed}
    backup=u.update(r,b)
    assert json.loads((backup/'update.json').read_text())['changed']==[changed]
    assert [str(p.relative_to(backup/'files')) for p in (backup/'files').rglob('*') if p.is_file()]==[changed]
    assert all((r/rel).stat().st_ino==ino for rel,ino in inodes.items())
    u.restore(r,backup);assert u.files(r)==before
    assert all((r/rel).stat().st_ino==ino for rel,ino in inodes.items())
    for nth in range(1,4):
        # Reuse the single-file payload but a fresh installed root.
        rr,aa,bb=setup(base/('selective-failure-'+str(nth)))
        initial=u.files(rr);original=s.atomic;counter=[0]
        def selective_fail(path,data,mode=0o644):
            if str(path).endswith('update.json'):return original(path,data,mode)
            counter[0]+=1
            if counter[0]==nth:raise OSError('injected selective write failure')
            return original(path,data,mode)
        try:
            with patch.object(s,'atomic',selective_fail):u.update(rr,b)
        except OSError:pass
        else:raise AssertionError('missing selective failure')
        assert u.files(rr)==initial
    # A changed-path list is not trusted merely because hashes are valid.
    backup=backup.rename(backup.with_name(backup.name+'.restored'))
    backup=u.update(r,b);j=json.loads((backup/'update.json').read_text());j['changed']=[]
    (backup/'update.json').write_text(json.dumps(j));unchanged=u.files(r)
    try:u.restore(r,backup)
    except ValueError:pass
    else:raise AssertionError('tampered changed-path manifest accepted')
    assert u.files(r)==unchanged
    # A legacy version-1 journal has complete backups and still restores.
    r,a,b=setup(base/'legacy');before=u.files(r);backup=u.update(r,b)
    j=json.loads((backup/'update.json').read_text());j['version']=1;del j['changed']
    (backup/'update.json').write_text(json.dumps(j))
    u.restore(r,backup);assert u.files(r)==before
    for nth in range(1,6):
        r,a,b=setup(base/str(nth));before=u.files(r);original=s.atomic;counter=[0]
        def fail(path,data,mode=0o644):
            if str(path).endswith('update.json'):return original(path,data,mode)
            counter[0]+=1
            if counter[0]==nth:raise OSError('injected before atomic write')
            return original(path,data,mode)
        try:
            with patch.object(s,'atomic',fail):u.update(r,b)
        except OSError:pass
        else:raise AssertionError('missing failure')
        assert u.files(r)==before
    for failure in ['installed','payload','backup','unrelated']:
        r,a,b=setup(base/failure);before=u.files(r)
        if failure=='installed':(r/s.PATHS['nord2-metal-probe']).write_bytes(b'other')
        if failure=='payload':(b/'nord2-metal-probe').write_bytes(b'other')
        if failure in ('backup','unrelated'):
            backup=u.update(r,b)
            path=backup/'files'/s.PATHS['nord2-metal-probe'] if failure=='backup' else r/s.LAUNCH
            path.write_bytes(b'other')
        unchanged=u.files(r)
        try:
            if failure in ('backup','unrelated'):u.restore(r,backup)
            else:u.update(r,b)
        except ValueError:pass
        else:raise AssertionError('missing refusal')
        assert u.files(r)==unchanged
print('PASS reversible addon upgrade, original uninstall preserved, five write failures and four tamper refusals')

# The device committer admits only the exact approved addon and fresh verified
# source. This exercises guards without invoking adb or writing any device.
import commit_root_delta as delta
before={'sha256':'a'*64,'independent_full_hash_matches':True}
after={'before':'a'*64,'after':'b'*64,'addon_sha256':'c'*64,'offline_verified':True,'rehearsal':False}
m={'before':'a'*64,'after':'b'*64,'bytes':16<<30}
delta.check_receipts(before,after,m,'c'*64)
for target,key,value in [('before','independent_full_hash_matches',False),('after','offline_verified',False),('after','rehearsal',True),('after','addon_sha256','d'*64),('after','before','d'*64),('after','after','d'*64),('manifest','bytes',0)]:
    args={'before':dict(before),'after':dict(after),'manifest':dict(m)}
    args[target][key]=value
    try:delta.check_receipts(args['before'],args['after'],args['manifest'],'c'*64)
    except ValueError:pass
    else:raise AssertionError('unsafe delta accepted: '+key)
assert delta.classify('a','a','b')=='pending' and delta.classify('b','a','b')=='done'
try:delta.classify('c','a','b')
except RuntimeError:pass
else:raise AssertionError('unknown device preimage accepted')
print('PASS seven delta receipt refusals and current-device preimage guard')
