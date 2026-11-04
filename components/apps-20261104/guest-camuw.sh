set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; L=R+'/usr/lib'; dst=L+'/libredwoodcamui.dylib'; prev=dst+'.prev'
NEW='d57e53793fe6f1d0941751d7f99b560c52fb42247f8caf7a44d4f4d5078e114c'      # camera-userland-20261109/out/camui/libredwoodcamui.dylib
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
new=open('/src/libredwoodcamui.dylib','rb').read()
# check everything first: nothing is written unless the file can be put
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the dylib this script was written for'
assert new[:4]==bytes.fromhex('cffaedfe') and b'/usr/lib/libredwoodcamui.dylib\0' in new, 'payload is not the arm64 dylib named /usr/lib/libredwoodcamui.dylib'
like=L+'/dyld'; assert os.path.isfile(like) and not os.path.islink(L), 'no /usr/lib/dyld on this root'
assert not os.path.islink(dst) and not os.path.isdir(dst), 'something else is at '+dst
st=os.stat(like)
def put(path, data):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,0o755); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
if os.path.exists(dst) and sha(dst)==NEW:
    print('CAMUW_DYLIB_ALREADY', NEW[:12])
else:
    if os.path.exists(dst):
        old=open(dst,'rb').read(); put(prev, old); print('CAMUW_DYLIB_PREVIOUS_KEPT', hashlib.sha256(old).hexdigest()[:12])
    put(dst, new)
    assert sha(dst)==NEW and os.stat(dst).st_mode&0o7777==0o755
    print('CAMUW_DYLIB_INSTALLED', NEW[:12], 'mode %o uid %d gid %d'%(os.stat(dst).st_mode&0o7777, os.stat(dst).st_uid, os.stat(dst).st_gid))
# what it works with, said (not changed here): the three-camera plists and Camera's own executable
D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
CAM3={'AVCaptureSession.plist':'bda9b23b0dacb01a36fc9301502ac77a356f83333609bd0eba2917bee8db2729','CameraSetup.plist':'457d35df2713143e29d150cef39d898d747e0301a5b73cba2c93f171ea852880'}
for n,want in CAM3.items(): print('CAMUW_PLIST', n, 'three-camera' if sha(D+'/'+n)==want else 'NOT the three-camera one: '+sha(D+'/'+n)[:12]+' (no ultra-wide source: run guest-cam3.sh)')
exe=R+'/Applications/Camera.app/Camera'
print('CAMUW_CAMERA_EXECUTABLE', 'stock' if sha(exe)=='a0516cafe47507e80aabd60402983b7c279cb00d6c1da94303bbde1bd72a49ee' else 'NOT stock: '+sha(exe)[:12]+' (TCC kills a changed Camera: run guest-camlog-undo.sh)')
print('CAMUW_MARKERS', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-camera')))
print('CC_JOB_ADDED camera: /usr/lib/libredwoodcamui.dylib (Camera.app: ultra-wide and wide as single cameras, once the kernel inserts it)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
