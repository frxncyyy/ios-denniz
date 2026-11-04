set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-fbhwctl.sh: job removed (Metal receipt updated); binary, config (= gate) and receipt removed.
# The kernel's own defaults (kern.n2fbhw boot value, normally 0) apply again from the next boot.
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2fbhwctl'; CFG='/private/var/db/nord2-fbhw-mode'; REC='/private/var/db/nord2-fbhwctl.json'
KEY='/System/Library/LaunchDaemons/com.nord2.fbhwctl.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'launchd.plist differs from the Metal receipt: not touching it'
if KEY in cfg['LaunchDaemons']:
    del cfg['LaunchDaemons'][KEY]; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('FBHWCTL_UNDO job removed')
for p in (CFG, BIN, REC):
    if os.path.lexists(R+p): os.remove(R+p); print('FBHWCTL_UNDO removed', p)
print('CC_JOB_ADDED undo fbhwctl'); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
