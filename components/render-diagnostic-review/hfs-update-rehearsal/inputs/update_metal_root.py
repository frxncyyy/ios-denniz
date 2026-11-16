#!/usr/bin/env python3
"""Reversible update of an installed Metal addon on an OFFLINE mounted clone.
No device access. Preserve launchd and the initial installation rollback.
"""
from pathlib import Path
import argparse, hashlib, json, shutil, os
import stage_metal_root as s


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
    if before.keys()!=after.keys():raise ValueError('update changes owned path set')
    identity=hashlib.sha256(json.dumps(after,sort_keys=True).encode()).hexdigest()
    backup=s.checked(root,s.STATE+'/updates/'+identity);backup.mkdir(parents=True,exist_ok=False)
    for rel in before:
        target=backup/'files'/rel;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(s.checked(root,rel),target);s.sync(target)
        if s.sha(target)!=before[rel]:raise ValueError('backup verification failed')
    journal={'version':1,'phase':'prepared','before':before,'after':after,'record_before':record}
    s.atomic(backup/'update.json',(json.dumps(journal,indent=2)+'\n').encode())
    interrupted=dict(record,phase='updating',update_backup=str(backup.relative_to(root)))
    s.journal(state,interrupted)
    try:
        for rel in before:
            path=s.checked(root,rel);s.atomic(path,sources[rel].read_bytes(),path.stat().st_mode&0o777)
        if files(root)!=after:raise ValueError('replacement verification failed')
        if s.sha(s.checked(root,s.LAUNCH))!=record['installed_launchd_sha256']:raise ValueError('launchd changed during update')
        s.journal(state,dict(record,inventory=new_inventory,phase='committed',last_update=str(backup.relative_to(root))))
        journal['phase']='committed';s.atomic(backup/'update.json',(json.dumps(journal,indent=2)+'\n').encode())
    except Exception:
        restore(root,backup)
        raise
    return backup


def restore(root,backup):
    backup=s.checked(root,backup.relative_to(root))
    journal=json.loads((backup/'update.json').read_text())
    before=journal['before'];after=journal['after'];record=journal['record_before']
    if set(before)!=set(after) or set(files(root))!=set(before):raise ValueError('unexpected update path set')
    if s.sha(s.checked(root,s.LAUNCH))!=record['installed_launchd_sha256']:raise ValueError('unrelated launchd change')
    for rel,oldhash in before.items():
        if s.sha(s.checked(root,rel)) not in (oldhash,after[rel]):raise ValueError('unrelated addon edit')
        if s.sha(s.checked(backup/'files',rel))!=oldhash:raise ValueError('backup changed')
    for rel in before:
        path=s.checked(root,rel);s.atomic(path,(backup/'files'/rel).read_bytes(),path.stat().st_mode&0o777)
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
