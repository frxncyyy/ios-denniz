set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-camhd60a.sh (q10jp): D22/AVCaptureSession.plist with the back cameras' 1080p60 and 720p60 device formats on the Video-60 stream format
# (was Video-FES-60, which matches none of the driver's stream formats: CMCapture then ran HD 60 as 1080p30 -- run cam11). From the 4096x3072
# file (guest-still4096.sh) only; that file kept as AVCaptureSession.plist.pre-camhd60a. Undo: guest-camhd60a-undo.sh.
import hashlib, os, plistlib
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
NEW='b3420a780de8d6078c0d2d5ace892cd3a5a553abb81148e28b6174b8e5c9cb11'      # camera-userland-20261112-hd60/system/D22-hd60/AVCaptureSession.plist
OLD='8e90184ffdf634c03dbf3f317f28aa4b35b98546210457d2a257cb1a3c25edfa'      # camera-userland-20261109/system/D22-still4096/AVCaptureSession.plist
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
def sixty(av):
    return [(i, k, av['AVCaptureDevices'][i]['MultiStream']['AVCaptureDeviceFormats'][k]['CaptureStreamFormat']) for i in (0, 1) for k in ('1080p60', '720p60')]
cur=D+'/AVCaptureSession.plist'; bak=cur+'.pre-camhd60a'
new=open('/src/D22/AVCaptureSession.plist','rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the plist this script was written for'
assert all(f=='Video-60' for _,_,f in sixty(plistlib.loads(new))), 'payload: the 60 formats are not on Video-60'
ch=sha(cur)
assert ch in (NEW, OLD), 'D22/AVCaptureSession.plist is neither the 4096x3072 file nor the HD60 one: %s (install guest-still4096.sh first)'%ch
if ch==NEW:
    assert os.path.exists(bak) and sha(bak)==OLD, 'HD60 plist there but no 4096x3072 copy (*.pre-camhd60a): review'
    print('CAMHD60A_PLIST_ALREADY', NEW[:12])
else:
    st=os.stat(cur)
    put(bak, open(cur,'rb').read(), st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(bak)==OLD
    put(cur, new, st.st_mode&0o7777, st.st_uid, st.st_gid); assert sha(cur)==NEW
    print('CAMHD60A_PLIST_INSTALLED', NEW[:12], sixty(plistlib.loads(new)), '(the file as it was kept as AVCaptureSession.plist.pre-camhd60a)')
print('CC_JOB_ADDED camera: back cameras 1080p60/720p60 on the Video-60 stream format (D22/AVCaptureSession.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
