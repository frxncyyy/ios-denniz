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
key='/System/Library/LaunchDaemons/com.apple.CommCenter.plist'
job=cfg['LaunchDaemons'][key]
env=dict(job.get('EnvironmentVariables',{})); env['OS_ACTIVITY_DT_MODE']='YES'; job['EnvironmentVariables']=env
job['StandardErrorPath']='/dev/console'; job['StandardOutPath']='/dev/console'
new=plistlib.dumps(cfg,fmt=fmt)
tmp=LP+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp,os.stat(LP).st_mode&0o7777); os.chown(tmp,os.stat(LP).st_uid,os.stat(LP).st_gid); os.replace(tmp,LP)
assert plistlib.loads(open(LP,'rb').read())==cfg
print('CC_JOB_ADDED env', env)
RP=R+'/private/var/db/nord2-metal/receipt.json'
rc=json.load(open(RP)); old=rc['installed_launchd_sha256']
assert old==hashlib.sha256(raw).hexdigest()
rc['installed_launchd_sha256']=hashlib.sha256(new).hexdigest()
open(RP+'.tmp','w').write(json.dumps(rc,indent=2)+'\n'); os.replace(RP+'.tmp',RP)
print('CC_RECEIPT_UPDATED', old[:12], '->', rc['installed_launchd_sha256'][:12])
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
