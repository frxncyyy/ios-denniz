set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa'
OLD='b66f008bdf095aa818db90651bdee3f3103f076bf3d4aa15e42f8b449ac2140e'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, like):
    st=os.stat(like); tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-still4096'
ch=sha(cur)
# check first
assert ch in (NEW, OLD), 'D22/AVCaptureSession.plist is neither the 4096x3072 file nor the dual-wide one: %s'%ch
if ch==NEW: assert os.path.exists(bak) and sha(bak)==OLD, 'no dual-wide copy (*.pre-still4096) to put back'
if ch==NEW:
    put(cur, open(bak,'rb').read(), cur); assert sha(cur)==OLD
    print('STILL4096_UNDO_PLIST the 4032x3024 file back', OLD[:12])
else:
    print('STILL4096_UNDO_PLIST_ALREADY', OLD[:12])
if os.path.exists(bak): os.unlink(bak)
print('CC_JOB_ADDED camera: back cameras photograph at 4032x3024 again (D22/AVCaptureSession.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
