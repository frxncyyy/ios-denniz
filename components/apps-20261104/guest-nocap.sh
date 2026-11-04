set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import plistlib, json, hashlib, os
R='/work/root'
LP=R+'/System/Library/xpc/launchd.plist'
raw=open(LP,'rb').read()
fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw)
job=cfg['LaunchDaemons']['/System/Library/LaunchDaemons/com.apple.mediaserverd.plist']
ms=job['MachServices']
# Nord 2 has no camera: mediaserverd never services the capture ports, so a synchronous AVCaptureDevice query
# (SpringBoard Control Center, sensor activity at every call) waits forever. Unlisted = lookup fails at once.
gone=[k for k in ('com.apple.coremedia.capturesource','com.apple.coremedia.capturesession','com.apple.coremedia.cameraviewfinder') if k in ms]
bk=R+'/private/var/db/nord2-metal/mediaserverd-machservices.pre-nocap.plist'
if not os.path.exists(bk): open(bk,'wb').write(plistlib.dumps({k:ms[k] for k in gone},fmt=plistlib.FMT_XML))
for k in gone: del ms[k]
new=plistlib.dumps(cfg,fmt=fmt)
tmp=LP+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp,os.stat(LP).st_mode&0o7777); os.chown(tmp,os.stat(LP).st_uid,os.stat(LP).st_gid); os.replace(tmp,LP)
assert plistlib.loads(open(LP,'rb').read())==cfg
print('NOCAP removed', gone, 'CC_JOB_ADDED')
RP=R+'/private/var/db/nord2-metal/receipt.json'
rc=json.load(open(RP)); old=rc['installed_launchd_sha256']
assert old==hashlib.sha256(raw).hexdigest()
rc['installed_launchd_sha256']=hashlib.sha256(new).hexdigest()
open(RP+'.tmp','w').write(json.dumps(rc,indent=2)+'\n'); os.replace(RP+'.tmp',RP)
print('NOCAP_RECEIPT_UPDATED CC_RECEIPT_UPDATED', old[:12], '->', rc['installed_launchd_sha256'][:12])
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
