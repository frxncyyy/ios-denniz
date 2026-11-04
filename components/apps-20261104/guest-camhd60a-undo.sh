set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-camhd60a-undo.sh: the 4096x3072 file (AVCaptureSession.plist.pre-camhd60a) back in place of the HD60 one. Nothing else.
import hashlib, os
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='b3420a780de8d6078c0d2d5ace892cd3a5a553abb81148e28b6174b8e5c9cb11'
OLD='8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, like):
    st=os.stat(like); tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-camhd60a'
ch=sha(cur)
assert ch in (NEW, OLD), 'D22/AVCaptureSession.plist is neither the HD60 file nor the 4096x3072 one: %s'%ch
if ch==NEW: assert os.path.exists(bak) and sha(bak)==OLD, 'no 4096x3072 copy (*.pre-camhd60a) to put back'
if ch==NEW:
    put(cur, open(bak,'rb').read(), cur); assert sha(cur)==OLD
    print('CAMHD60A_UNDO_PLIST the 4096x3072 file back', OLD[:12])
else:
    print('CAMHD60A_UNDO_PLIST_ALREADY', OLD[:12])
if os.path.exists(bak): os.unlink(bak)
print('CC_JOB_ADDED camera: 1080p60/720p60 back on Video-FES-60 (D22/AVCaptureSession.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
