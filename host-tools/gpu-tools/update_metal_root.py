#!/usr/bin/env python3
"""Reversible update of an installed Metal addon on an OFFLINE mounted clone.
No device access. Preserve launchd and the initial installation rollback.
"""
from pathlib import Path
import argparse, copy, hashlib, json, plistlib, shutil, os
import stage_metal_root as s

# Jobs an update may remove from launchd's cache (the cache is the only thing launchd loads here).  Anything else is refused: the list is a
# deliberate allow-list, not a pattern.  com.apple.voicemail.vmd busy-loops at ~95 % of a core without CommCenter.
REMOVABLE_JOBS = ('/System/Library/LaunchDaemons/com.apple.voicemail.vmd.plist',)


def launchd_removals(payload):
    """Jobs the payload manifest asks to remove from the launchd cache; refuses anything outside REMOVABLE_JOBS."""
    jobs=json.loads((payload/'manifest.json').read_text()).get('launchd_remove_jobs',[])
    if not isinstance(jobs,list) or len(set(jobs))!=len(jobs) or any(j not in REMOVABLE_JOBS for j in jobs):
        raise ValueError('launchd job removal outside the allow-list')
    return jobs


def launchd_without(data,jobs):
    """New cache bytes with `jobs` removed from LaunchDaemons; every other key and value untouched.  (None, 0) when none of them is there."""
    config=plistlib.loads(data)
    daemons=config.get('LaunchDaemons')
    if not isinstance(daemons,dict):raise ValueError('unexpected launchd database')
    present=[j for j in jobs if j in daemons]
    if not present:return None,0
    new=copy.deepcopy(config)
    for j in present:del new['LaunchDaemons'][j]
    out=plistlib.dumps(new,fmt=plistlib.FMT_BINARY,sort_keys=False)
    check=plistlib.loads(out);expected=copy.deepcopy(config)
    for j in present:del expected['LaunchDaemons'][j]
    if check!=expected or any(j in check['LaunchDaemons'] for j in present):raise ValueError('launchd rewrite verification failed')
    return out,len(present)


def files(root):
    result={}
    for rel in s.PATHS.values():
        path=s.checked(root,rel)
        for key,value in s.inventory(path).items():
            if value!='directory': result[rel if key=='.' else rel+'/'+key]=value
    return result


def update(root,payload):
    s.validate_payload(payload)
    state=s.checked(root,s.STATE);record=json.loads((state/'receipt.json').read_text())
    if record.get('version')!=2 or set(record.get('inventory',{}))!=set(s.PATHS.values()):raise ValueError('unexpected installation receipt')
    if record.get('phase')!='committed':raise ValueError('installation is not committed')
    if s.sha(s.checked(root,s.LAUNCH))!=record['installed_launchd_sha256']:raise ValueError('launchd changed')
    if s.sha(state/'launchd.original')!=record['original_launchd_sha256']:raise ValueError('original backup changed')
    for rel,inventory in record['inventory'].items():
        if s.inventory(s.checked(root,rel))!=inventory:raise ValueError('installed addon changed: '+rel)
    before=files(root);after={};new_inventory={};sources={}
    for name,rel in s.PATHS.items():
        inv=s.inventory(s.checked(payload,name));new_inventory[rel]=inv
        for key,value in inv.items():
            if value=='directory':continue
            target=rel if key=='.' else rel+'/'+key
            after[target]=value;sources[target]=payload/name if key=='.' else payload/name/key
    if before.keys()-after.keys():raise ValueError('update removes owned paths')
    # New files are allowed only inside an owned directory that already exists
    # (e.g. a pipeline pack in the bundle); the journal lists them for restore.
    added=sorted(after.keys()-before.keys())
    owned_dirs=[rel for name,rel in s.PATHS.items() if s.checked(payload,name).is_dir()]
    for rel in added:
        if not any(rel.startswith(d+'/') for d in owned_dirs):raise ValueError('added path outside owned directory: '+rel)
        target=s.checked(root,rel)
        if not target.parent.is_dir() or target.exists() or target.with_name(target.name+'.nord2-new').exists():
            raise ValueError('added path unavailable: '+rel)
    changed=[rel for rel in before if before[rel]!=after[rel]]
    launch_path=s.checked(root,s.LAUNCH);launch_old=launch_path.read_bytes()
    launch_new,removed_count=launchd_without(launch_old,launchd_removals(payload))
    if not changed and not added and launch_new is None:raise ValueError('addon is already current')
    identity=hashlib.sha256((json.dumps(after,sort_keys=True)+('' if launch_new is None else 'launchd:'+hashlib.sha256(launch_new).hexdigest())).encode()).hexdigest()
    backup=s.checked(root,s.STATE+'/updates/'+identity);backup.mkdir(parents=True,exist_ok=False)
    for rel in changed:
        target=backup/'files'/rel;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(s.checked(root,rel),target);s.sync(target)
        if s.sha(target)!=before[rel]:raise ValueError('backup verification failed')
    journal={'version':3 if added else 2,'phase':'prepared','before':before,'after':after,'changed':changed,'record_before':record}
    if added:journal['added']=added
    if launch_new is not None:
        # launchd's cache before the removal, byte for byte, and both hashes: restore() refuses anything else.
        s.atomic(backup/'launchd.before',launch_old)
        if s.sha(backup/'launchd.before')!=s.sha(launch_path):raise ValueError('launchd backup verification failed')
        journal['launchd']={'before':hashlib.sha256(launch_old).hexdigest(),'after':hashlib.sha256(launch_new).hexdigest(),'removed':removed_count}
    s.atomic(backup/'update.json',(json.dumps(journal,indent=2)+'\n').encode())
    interrupted=dict(record,phase='updating',update_backup=str(backup.relative_to(root)))
    s.journal(state,interrupted)
    try:
        for rel in changed:
            path=s.checked(root,rel);s.atomic(path,sources[rel].read_bytes(),path.stat().st_mode&0o777)
        for rel in added:
            s.atomic(s.checked(root,rel),sources[rel].read_bytes(),sources[rel].stat().st_mode&0o777)
        if files(root)!=after:raise ValueError('replacement verification failed')
        if s.sha(s.checked(root,s.LAUNCH))!=record['installed_launchd_sha256']:raise ValueError('launchd changed during update')
        extra={}
        if launch_new is not None:
            s.atomic(launch_path,launch_new,launch_path.stat().st_mode&0o777)
            if launch_path.read_bytes()!=launch_new:raise ValueError('launchd replacement verification failed')
            extra['installed_launchd_sha256']=journal['launchd']['after']
        s.journal(state,dict(record,inventory=new_inventory,phase='committed',last_update=str(backup.relative_to(root)),**extra))
        journal['phase']='committed';s.atomic(backup/'update.json',(json.dumps(journal,indent=2)+'\n').encode())
    except Exception:
        restore(root,backup)
        raise
    return backup


def restore(root,backup):
    backup=s.checked(root,backup.relative_to(root))
    journal=json.loads((backup/'update.json').read_text())
    before=journal['before'];after=journal['after'];record=journal['record_before']
    version=journal.get('version');added=journal.get('added',[]) if version==3 else []
    if version==3:
        if not added or set(after)!=set(before)|set(added) or set(before)&set(added):raise ValueError('unexpected update path set')
    elif set(before)!=set(after):raise ValueError('unexpected update path set')
    # An interrupted update may have created only some of the added files.
    current=set(files(root))
    if not set(before)<=current<=set(before)|set(added):raise ValueError('unexpected update path set')
    if version==1:
        changed=list(before)  # Existing complete backups remain restorable.
    elif version in (2,3):
        changed=[rel for rel in before if before[rel]!=after[rel]]
        if journal.get('changed')!=changed:raise ValueError('changed-path manifest mismatch')
    else:raise ValueError('unknown update journal version')
    for rel in added:
        if rel in current and s.sha(s.checked(root,rel))!=after[rel]:raise ValueError('unrelated addon edit')
    lj=journal.get('launchd')
    launch_path=s.checked(root,s.LAUNCH);launch_now=s.sha(launch_path)
    if lj is None:
        if launch_now!=record['installed_launchd_sha256']:raise ValueError('unrelated launchd change')
    else:
        # the update may have been interrupted before or after the cache was rewritten; nothing else is acceptable
        if lj['before']!=record['installed_launchd_sha256'] or launch_now not in (lj['before'],lj['after']):raise ValueError('unrelated launchd change')
        if s.sha(backup/'launchd.before')!=lj['before']:raise ValueError('launchd backup changed')
    for rel,oldhash in before.items():
        if s.sha(s.checked(root,rel)) not in (oldhash,after[rel]):raise ValueError('unrelated addon edit')
        if rel in changed and s.sha(s.checked(backup/'files',rel))!=oldhash:raise ValueError('backup changed')
    for rel in changed:
        path=s.checked(root,rel);s.atomic(path,(backup/'files'/rel).read_bytes(),path.stat().st_mode&0o777)
    for rel in added:
        if rel in current:
            path=s.checked(root,rel);path.unlink();s.sync(path.parent)
    if lj is not None and launch_now==lj['after']:
        s.atomic(launch_path,(backup/'launchd.before').read_bytes(),launch_path.stat().st_mode&0o777)
    if lj is not None and s.sha(launch_path)!=lj['before']:raise ValueError('launchd restore verification failed')
    if set(files(root))!=set(before):raise ValueError('restore verification failed')
    s.journal(s.checked(root,s.STATE),record)
    journal['phase']='restored';s.atomic(backup/'update.json',(json.dumps(journal,indent=2)+'\n').encode())


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('operation',choices=['update','restore']);ap.add_argument('root',type=Path);ap.add_argument('--payload',type=Path);ap.add_argument('--backup',type=Path);a=ap.parse_args();root=a.root.resolve()
    if os.geteuid()!=0 or root==Path('/') or not root.is_mount():ap.error('requires root and an explicitly mounted offline clone')
    if a.operation=='update':
        if not a.payload:ap.error('--payload required')
        print('VERIFIED_UPDATE_BACKUP='+str(update(root,a.payload.resolve())))
    else:
        if not a.backup:ap.error('--backup required')
        restore(root,a.backup.resolve());print('VERIFIED_UPDATE_RESTORED')
