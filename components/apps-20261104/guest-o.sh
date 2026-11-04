set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, plistlib, hashlib
R='/work/root'
p=R+'/private/var/mobile/Library/Preferences/com.apple.imdsmsrecordstore.plist'
print('existed', os.path.exists(p))
data=open('/src/com.apple.imdsmsrecordstore.plist','rb').read()
assert hashlib.sha256(data).hexdigest()=='baa353f5f85e965136d1f16ff1ead36cbcec4386eaee0346a8865a3123933b7a'
if os.path.exists(p):
    cfg=plistlib.loads(open(p,'rb').read()); cfg['kIMDPersistenceLastBuild']='20H392'; data=plistlib.dumps(cfg,fmt=plistlib.FMT_BINARY)
tmp=p+'.nord2tmp'; open(tmp,'wb').write(data); os.chown(tmp,501,501); os.chmod(tmp,0o600); os.replace(tmp,p)
print('CC_JOB_ADDED', plistlib.loads(open(p,'rb').read()))
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
