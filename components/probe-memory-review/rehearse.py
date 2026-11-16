from pathlib import Path
import json,sys,tarfile,tempfile,plistlib,hashlib
p=Path.cwd();sys.path.insert(0,str(p/'gpu/tools'));import update_metal_root as u
s=u.s;old=p/'gpu/build/device-tiler-v5/nord2-metal-addon.tar.gz';new=p/'gpu/build/device-tiler-memory-v6/nord2-metal-addon.tar.gz'
with tempfile.TemporaryDirectory(prefix='nord2-probe-memory-') as tmp:
 t=Path(tmp)
 for name,archive in [('old',old),('new',new)]:
  with tarfile.open(archive) as f:f.extractall(t/name,filter='data')
  s.validate_payload(t/name/'payload')
 oldm=json.loads((t/'old/payload/manifest.json').read_text())['files'];newm=json.loads((t/'new/payload/manifest.json').read_text())['files']
 assert oldm.keys()==newm.keys()
 changed=[k for k in oldm if oldm[k]!=newm[k]];assert changed==['nord2-metal-probe'],changed
 assert s.sha(p/'gpu/build/native/nord2-metal-probe')==newm['nord2-metal-probe']
 r=t/'root';launch=r/s.LAUNCH;launch.parent.mkdir(parents=True)
 original=plistlib.dumps({'LaunchDaemons':{'backboardd':{'Program':'/usr/libexec/backboardd'},'wifid':{'Program':'/usr/sbin/wifid'}}});launch.write_bytes(original)
 s.apply(r,t/'old/payload');before=u.files(r);installed=launch.read_bytes()
 inodes={k:(r/k).stat().st_ino for k in before if k!=s.PATHS['nord2-metal-probe']}
 backup=u.update(r,t/'new/payload');after=u.files(r)
 assert [k for k in before if before[k]!=after[k]]==[s.PATHS['nord2-metal-probe']]
 backed=[f for f in (backup/'files').rglob('*') if f.is_file()];assert len(backed)==1
 backed_bytes=sum(f.stat().st_size for f in backed)
 assert launch.read_bytes()==installed and all((r/k).stat().st_ino==i for k,i in inodes.items())
 u.restore(r,backup);assert u.files(r)==before and launch.read_bytes()==installed
 assert all((r/k).stat().st_ino==i for k,i in inodes.items())
 s.rollback(r);assert launch.read_bytes()==original
 out={'old_sha256':s.sha(old),'new_sha256':s.sha(new),'changed_payload':changed,'backup_bytes':backed_bytes,
 'new_probe_bytes':(t/'new/payload/nord2-metal-probe').stat().st_size,'update_restore_uninstall':'PASS',
 'unchanged_payload_inodes_preserved':True,'launchd_preserved':True,'host_fixture_only':True,'device_tested':False}
 (p/'gpu/build/device-tiler-memory-v6/update-rehearsal.json').write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
