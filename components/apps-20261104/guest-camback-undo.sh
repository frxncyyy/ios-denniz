set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, plistlib
R='/work/root'; PREFS=R+'/private/var/mobile/Library/Preferences/com.apple.camera.plist'; KEEP=PREFS+'.pre-camback'
if not os.path.exists(KEEP):
    print('CAMBACK_UNDO_NOTHING_KEPT')
else:
    keep=plistlib.loads(open(KEEP,'rb').read()); assert set(keep)=={'was','absent'}
    if os.path.isfile(PREFS):
        st=os.stat(PREFS); prefs=plistlib.loads(open(PREFS,'rb').read())
        prefs.update(keep['was'])
        for k in keep['absent']: prefs.pop(k, None)
        tmp=PREFS+'.nord2tmp'
        with open(tmp,'wb') as f: f.write(plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY)); f.flush(); os.fsync(f.fileno())
        os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,PREFS)
    os.unlink(KEEP)
    print('CAMBACK_UNDONE', keep['was'], 'removed', keep['absent'])
print('CC_JOB_ADDED camera: Camera.app start camera as it was')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
