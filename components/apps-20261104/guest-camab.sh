set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; D=R+'/System/Library/PrivateFrameworks/CMCapture.framework/D22'
STOCK={'AVCaptureSession.plist':'f240baa42e3b72e9f76b4ceff1b813fd64ed136a5463f6523db1d404520c5343','CameraSetup.plist':'c009561b618812be105a037da4d1b545724ba73d93f4310b53945f4cbfd21ec4'}
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
for n,stock in STOCK.items():
    cur=D+'/'+n
    if sha(cur)==stock: print('CAMAB_PLIST_ALREADY_STOCK', n); continue
    assert sha(cur+'.pre-cam')==stock, 'no stock backup of '+n
    st=os.stat(cur)
    def put(path,data):
        tmp=path+'.nord2tmp'; open(tmp,'wb').write(data); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    put(cur+'.cam3', open(cur,'rb').read()); put(cur, open(cur+'.pre-cam','rb').read())
    assert sha(cur)==stock; print('CAMAB_PLIST_STOCK', n)
m=R+'/private/var/db/nord2-camera-backonly'
open(m,'wb').close(); os.chmod(m,0o644); print('CAMAB_MARKER', os.path.exists(m))
print('CC_JOB_ADDED camera A/B: stock plists, back wide only')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
