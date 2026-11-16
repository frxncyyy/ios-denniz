from pathlib import Path
import sys,tempfile,tarfile,json,plistlib,hashlib
r=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(r/'tools'))
import update_metal_root as u
s=u.s
with tempfile.TemporaryDirectory(prefix='nord2-idvs-payload-') as temp:
 b=Path(temp);root=b/'root'
 for version,folder in [('old','device-geometry-v3'),('new','device-idvs-v4')]:
  with tarfile.open(r/'build'/folder/'nord2-metal-addon.tar.gz') as tar:tar.extractall(b/version,filter='data')
 old=b/'old/payload';new=b/'new/payload';s.validate_payload(old);s.validate_payload(new)
 launch=root/s.LAUNCH;launch.parent.mkdir(parents=True)
 original=plistlib.dumps({'LaunchDaemons':{'backboardd':{'Program':'/usr/libexec/backboardd'},'wifid':{'Program':'/usr/sbin/wifid'},'mediaserverd':{'Program':'/usr/sbin/mediaserverd'}}})
 launch.write_bytes(original)
 s.apply(root,old);before=u.files(root);installed_launch=launch.read_bytes()
 backup=u.update(root,new);after=u.files(root)
 assert launch.read_bytes()==installed_launch
 a=json.loads((old/'manifest.json').read_text())['files'];c=json.loads((new/'manifest.json').read_text())['files']
 changed=[k for k in a if a[k]!=c[k]]
 assert set(changed)=={'Nord2Metal.bundle/Nord2Metal','nord2-metal-probe'},changed
 for name,digest in c.items():
  parts=name.split("/",1);dest=root/s.PATHS[parts[0]]
  if len(parts)>1:dest=dest/parts[1]
  assert s.sha(dest)==digest
 u.restore(root,backup);assert u.files(root)==before
 s.rollback(root);assert launch.read_bytes()==original
 result={'actual_archives':True,'update_restore_uninstall':'PASS','launchd_preserved':True,'changed_payload_files':changed,'device_access':False}
 (r/'build/idvs-review/payload-test.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result))
