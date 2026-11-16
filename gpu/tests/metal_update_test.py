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

# An update may add a file inside an owned directory (a pipeline pack in the
# bundle); restore removes it again. Removals and paths outside are refused.
def add_pack(payload,data):
    (payload/'Nord2Metal.bundle/PipelinePack.tpcpack').write_bytes(data)
    (payload/'manifest.json').write_text(json.dumps({'version':1,'files':{
        str(p.relative_to(payload)):s.sha(p) for p in payload.rglob('*') if p.is_file() and p.name!='manifest.json'}}))
with tempfile.TemporaryDirectory() as temp:
    base=Path(temp);pack='System/Library/Extensions/Nord2Metal.bundle/PipelinePack.tpcpack'
    r,a,b=setup(base/'added');add_pack(b,b'TPCPACK'+b'\x01'*64);before=u.files(r)
    backup=u.update(r,b);j=json.loads((backup/'update.json').read_text())
    assert j['version']==3 and j['added']==[pack] and u.files(r)[pack]==s.sha(b/'Nord2Metal.bundle/PipelinePack.tpcpack')
    u.restore(r,backup);assert u.files(r)==before and not (r/pack).exists()
    # pack-only update (everything else identical) is not "already current"
    r,a,b=setup(base/'pack-only')
    for p in a.rglob('*'):
        if p.is_file() and p.name!='manifest.json':(b/p.relative_to(a)).write_bytes(p.read_bytes())
    add_pack(b,b'TPCPACK2');before=u.files(r);backup=u.update(r,b)
    assert json.loads((backup/'update.json').read_text())['changed']==[]
    u.restore(r,backup);assert u.files(r)==before
    # a failure while writing the added file rolls everything back
    r,a,b=setup(base/'added-fail');add_pack(b,b'TPCPACK3');initial=u.files(r);original=s.atomic
    def fail_on_pack(path,data,mode=0o644):
        if path.name=='PipelinePack.tpcpack':raise OSError('injected pack write failure')
        return original(path,data,mode)
    try:
        with patch.object(s,'atomic',fail_on_pack):u.update(r,b)
    except OSError:pass
    else:raise AssertionError('missing pack failure')
    assert u.files(r)==initial and not (r/pack).exists()
    # a tampered added file blocks restore
    r,a,b=setup(base/'added-tamper');add_pack(b,b'TPCPACK4');backup=u.update(r,b)
    (r/pack).write_bytes(b'other')
    try:u.restore(r,backup)
    except ValueError:pass
    else:raise AssertionError('tampered added file restored')
    # removing an owned file is refused
    r,a,b=setup(base/'removed');(b/'Nord2Metal.bundle/Info.plist').unlink()
    (b/'manifest.json').write_text(json.dumps({'version':1,'files':{
        str(p.relative_to(b)):s.sha(p) for p in b.rglob('*') if p.is_file() and p.name!='manifest.json'}}))
    try:u.update(r,b)
    except (ValueError,OSError):pass  # refused by payload validation or the path-set check
    else:raise AssertionError('owned path removal accepted')
print('PASS added owned files: journal v3, restore removes them, failure rollback, tamper and removal refusals')

# An update may also remove a deliberately allow-listed job from launchd's cache (com.apple.voicemail.vmd).  Everything else in the cache stays
# byte-for-byte equal as parsed data; restore puts the original bytes back; unlisted jobs and tampering are refused.
VMD='/System/Library/LaunchDaemons/com.apple.voicemail.vmd.plist'
def with_vmd(root):
    launch=root/s.LAUNCH;config=plistlib.loads(launch.read_bytes())
    config['LaunchDaemons'][VMD]={'Program':'/System/Library/PrivateFrameworks/VisualVoicemail.framework/vmd','MachServices':{'com.apple.voicemail.vmd':True}}
    config['AppExtensions']={'/Applications/MobilePhone.app':{'x':1}}
    launch.write_bytes(plistlib.dumps(config,fmt=plistlib.FMT_BINARY,sort_keys=False))
def ask_removal(payload,jobs):
    m=json.loads((payload/'manifest.json').read_text());m['launchd_remove_jobs']=jobs;(payload/'manifest.json').write_text(json.dumps(m))
with tempfile.TemporaryDirectory() as temp:
    base=Path(temp)
    r,a,b=setup(base/'vmd');with_vmd(r);ask_removal(b,[VMD]);before=u.files(r);launch_before=(r/s.LAUNCH).read_bytes()
    record=json.loads((r/s.STATE/'receipt.json').read_text());record['installed_launchd_sha256']=s.sha(r/s.LAUNCH);s.journal(r/s.STATE,record)
    original=plistlib.loads(launch_before)
    backup=u.update(r,b);now=plistlib.loads((r/s.LAUNCH).read_bytes())
    assert VMD not in now['LaunchDaemons'] and set(now['LaunchDaemons'])==set(original['LaunchDaemons'])-{VMD}
    assert {k:v for k,v in now.items() if k!='LaunchDaemons'}=={k:v for k,v in original.items() if k!='LaunchDaemons'}
    assert all(now['LaunchDaemons'][k]==v for k,v in original['LaunchDaemons'].items() if k!=VMD)
    j=json.loads((backup/'update.json').read_text());assert j['launchd']['removed']==1 and (backup/'launchd.before').read_bytes()==launch_before
    assert json.loads((r/s.STATE/'receipt.json').read_text())['installed_launchd_sha256']==s.sha(r/s.LAUNCH)
    # a second run of the same payload is a no-op update and is refused, a later ordinary update still works (receipt carries the new hash)
    try:u.update(r,b)
    except ValueError:pass
    else:raise AssertionError('repeated removal accepted')
    u.restore(r,backup);assert u.files(r)==before and (r/s.LAUNCH).read_bytes()==launch_before
    assert json.loads((r/s.STATE/'receipt.json').read_text())['installed_launchd_sha256']==s.sha(r/s.LAUNCH)
    # removal only (files already current) gets its own backup directory; a full uninstall afterwards brings the ORIGINAL back
    backup=backup.rename(backup.with_name(backup.name+'.restored'))
    backup=u.update(r,b);s.rollback(r)
    assert not any((r/x).exists() for x in s.PATHS.values())
    # failure injection at every write, with the cache written or not yet written
    for nth in range(1,9):
        r,a,b=setup(base/('vmd-fail-%d'%nth));with_vmd(r);ask_removal(b,[VMD])
        record=json.loads((r/s.STATE/'receipt.json').read_text());record['installed_launchd_sha256']=s.sha(r/s.LAUNCH);s.journal(r/s.STATE,record)
        before=u.files(r);launch_before=(r/s.LAUNCH).read_bytes();orig=s.atomic;counter=[0]
        def fail(path,data,mode=0o644):
            if str(path).endswith('update.json') or str(path).endswith('launchd.before'):return orig(path,data,mode)
            counter[0]+=1
            if counter[0]==nth:raise OSError('injected write failure')
            return orig(path,data,mode)
        try:
            with patch.object(s,'atomic',fail):u.update(r,b)
        except OSError:pass
        else:
            assert nth>7,'write %d was expected to fail'%nth   # 7 writes: interrupted journal, four files, launchd, final journal
            continue
        assert nth<=7 and u.files(r)==before and (r/s.LAUNCH).read_bytes()==launch_before
        assert json.loads((r/s.STATE/'receipt.json').read_text())['phase']=='committed'
    # refusals: a job outside the allow-list, a tampered backup, a launchd that is neither the old nor the new bytes
    r,a,b=setup(base/'vmd-refuse');with_vmd(r)
    record=json.loads((r/s.STATE/'receipt.json').read_text());record['installed_launchd_sha256']=s.sha(r/s.LAUNCH);s.journal(r/s.STATE,record)
    ask_removal(b,['/System/Library/LaunchDaemons/com.apple.backboardd.plist'])
    launch_before=(r/s.LAUNCH).read_bytes()
    try:u.update(r,b)
    except ValueError:pass
    else:raise AssertionError('removal outside the allow-list accepted')
    assert (r/s.LAUNCH).read_bytes()==launch_before
    ask_removal(b,[VMD]);backup=u.update(r,b)
    (backup/'launchd.before').write_bytes(b'other')
    try:u.restore(r,backup)
    except ValueError:pass
    else:raise AssertionError('tampered launchd backup accepted')
    (backup/'launchd.before').write_bytes(launch_before);(r/s.LAUNCH).write_bytes(b'something else')
    try:u.restore(r,backup)
    except ValueError:pass
    else:raise AssertionError('unrelated launchd accepted')
print('PASS launchd job removal: exact removal, restore byte for byte, uninstall, write-failure rollback, three refusals')
