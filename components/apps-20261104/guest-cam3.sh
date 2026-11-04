set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
# the three-camera plists of camera-userland-20261109/system/D22 (tools/make_camera_plists.py), kept as *.cam3 by guest-camab.sh
CAM3={'AVCaptureSession.plist':'bda9b23b0dacb01a36fc9301502ac77a356f83333609bd0eba2917bee8db2729','CameraSetup.plist':'457d35df2713143e29d150cef39d898d747e0301a5b73cba2c93f171ea852880'}
STOCK={'AVCaptureSession.plist':'f240baa42e3b72e9f76b4ceff1b813fd64ed136a5463f6523db1d404520c5343','CameraSetup.plist':'c009561b618812be105a037da4d1b545724ba73d93f4310b53945f4cbfd21ec4'}
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
# check everything first: nothing is written unless both files can be put
for n,want in CAM3.items():
    cur=D+'/'+n
    if sha(cur)==want: continue
    assert os.path.exists(cur+'.cam3') and sha(cur+'.cam3')==want, 'no three-camera copy of '+n+' (*.cam3)'
    assert sha(cur)==STOCK[n] or (os.path.exists(cur+'.pre-cam') and sha(cur+'.pre-cam')==STOCK[n]), 'no stock copy of '+n+' would be left'
for n,want in CAM3.items():
    cur=D+'/'+n
    if sha(cur)==want: print('CAM3_PLIST_ALREADY_THREE', n); continue
    st=os.stat(cur)
    def put(path,data):
        tmp=path+'.nord2tmp'; open(tmp,'wb').write(data); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    if not (os.path.exists(cur+'.pre-cam') and sha(cur+'.pre-cam')==STOCK[n]):
        put(cur+'.pre-cam', open(cur,'rb').read())          # (the stock one stays as *.pre-cam, as guest-cam.sh left it)
    put(cur, open(cur+'.cam3','rb').read())
    assert sha(cur)==want and sha(cur+'.pre-cam')==STOCK[n]; print('CAM3_PLIST_THREE', n)
m=R+'/private/var/db/nord2-camera-backonly'
if os.path.lexists(m): os.remove(m)
print('CAM3_MARKER_GONE', not os.path.lexists(m))
print('CC_JOB_ADDED camera: three-camera plists (wide, ultra-wide, front), back-only marker removed')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
