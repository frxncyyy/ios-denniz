set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os, plistlib
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='b66f008bdf095aa818db90651bdee3f3103f076bf3d4aa15e42f8b449ac2140e'
CAM3='bda9b23b0dacb01a36fc9301502ac77a356f83333609bd0eba2917bee8db2729'
PREFS=R+'/private/var/mobile/Library/Preferences/com.apple.camera.plist'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, like):
    st=os.stat(like); tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-camdual'; pbak=PREFS+'.pre-camdual'; pabsent=PREFS+'.pre-camdual-absent'
ch=sha(cur)
# check first
assert ch in (NEW, CAM3), 'D22/AVCaptureSession.plist is neither the dual-wide file nor the three-camera one: %s'%ch
if ch==NEW: assert os.path.exists(bak) and sha(bak)==CAM3, 'no three-camera copy (*.pre-camdual) to put back'
if os.path.exists(pbak): plistlib.loads(open(pbak,'rb').read())
if ch==NEW:
    put(cur, open(bak,'rb').read(), cur); assert sha(cur)==CAM3
    print('CAMDUAL_UNDO_PLIST three-camera file back', CAM3[:12])
else:
    print('CAMDUAL_UNDO_PLIST_ALREADY', CAM3[:12])
if os.path.exists(bak): os.unlink(bak)
if os.path.exists(pbak):
    put(PREFS, open(pbak,'rb').read(), pbak); os.unlink(pbak); print('CAMDUAL_UNDO_PREFS as they were')
elif os.path.exists(pabsent):
    if os.path.exists(PREFS): os.unlink(PREFS)
    os.unlink(pabsent); print('CAMDUAL_UNDO_PREFS removed (there were none)')
else:
    print('CAMDUAL_UNDO_PREFS_NOTHING_KEPT (left as they are)')
print('CC_JOB_ADDED camera: dual-wide device and its Camera preferences undone')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
