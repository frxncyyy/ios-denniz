set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, plistlib
R='/work/root'; PREFS=R+'/private/var/mobile/Library/Preferences/com.apple.camera.plist'; KEEP=PREFS+'.pre-camback'
WANT={'CAMUserPreferenceCaptureDevice': 0, 'CAMUserPreferenceCaptureMode': 0}
assert os.path.isfile(PREFS), 'no com.apple.camera.plist (Camera.app has never run?): nothing to change'
st=os.stat(PREFS); prefs=plistlib.loads(open(PREFS,'rb').read()); assert isinstance(prefs, dict)
def put(path, data):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
was={k: prefs[k] for k in WANT if k in prefs}
if all(prefs.get(k)==v and type(prefs.get(k)) is int for k,v in WANT.items()):
    print('CAMBACK_ALREADY', was)
else:
    if not os.path.exists(KEEP): put(KEEP, plistlib.dumps({'was': was, 'absent': sorted(k for k in WANT if k not in prefs)}, fmt=plistlib.FMT_BINARY))
    prefs.update(WANT); put(PREFS, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY))
    assert all(plistlib.loads(open(PREFS,'rb').read()).get(k)==v for k,v in WANT.items())
    print('CAMBACK_SET', WANT, 'were', was)
print('CC_JOB_ADDED camera: Camera.app starts on the back camera, Photo')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
