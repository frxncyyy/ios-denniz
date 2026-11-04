set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import plistlib, json, hashlib, os, copy
R='/work/root'
LP=R+'/System/Library/xpc/launchd.plist'
raw=open(LP,'rb').read()
fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw)
old='/System/Library/LaunchDaemons/com.apple.CommCenter-noncellular.plist'
key='/System/Library/LaunchDaemons/com.apple.CommCenter.plist'
assert key not in cfg['LaunchDaemons']
cfg['LaunchDaemons'].pop(old,None)
job=plistlib.loads(open(R+key,'rb').read())
job.pop('_LimitLoadToDeviceTree',None)
assert job['Label']=='com.apple.CommCenter' and os.path.exists(R+job['ProgramArguments'][0])
before=len(cfg['LaunchDaemons'])
cfg['LaunchDaemons'][key]=job
new=plistlib.dumps(cfg,fmt=fmt)
tmp=LP+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp,os.stat(LP).st_mode&0o7777); os.chown(tmp,os.stat(LP).st_uid,os.stat(LP).st_gid); os.replace(tmp,LP)
assert plistlib.loads(open(LP,'rb').read())==cfg
print('CC_JOB_ADDED', before, '->', len(cfg['LaunchDaemons']), 'format', 'bin' if fmt==plistlib.FMT_BINARY else 'xml')
# keep the Metal updater's consistency check happy (update_metal_root: installed_launchd_sha256 must equal the cache)
RP=R+'/private/var/db/nord2-metal/receipt.json'
rc=json.load(open(RP))
old=rc['installed_launchd_sha256']
assert old==hashlib.sha256(raw).hexdigest(), 'cache differs from the receipt before the edit'
rc['installed_launchd_sha256']=hashlib.sha256(new).hexdigest()
open(RP+'.tmp','w').write(json.dumps(rc,indent=2)+'\n'); os.replace(RP+'.tmp',RP)
print('CC_RECEIPT_UPDATED', old[:12], '->', rc['installed_launchd_sha256'][:12])
os.sync()
# Mav17 firmware folder for ABM/BBUpdater (fallback path), unzipped from the bbfw already on the rootfs
import zipfile
src=R+'/usr/standalone/update/Mav17.Release.bbfw'; dst=R+'/usr/standalone/firmware/Baseband/Mav17'
os.makedirs(dst, exist_ok=True)
with zipfile.ZipFile(src) as z:
    names=[n for n in z.namelist() if not n.endswith('/')]
    for n in names:
        out=os.path.join(dst, os.path.basename(n))
        with z.open(n) as f, open(out,'wb') as g: g.write(f.read())
        os.chmod(out,0o644)
print('MAV17_FW', len(names), sorted(os.path.basename(n) for n in names)[:12])
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
