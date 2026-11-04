set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; APP=R+'/Applications/Camera.app'
STOCK='a0516cafe47507e80aabd60402983b7c279cb00d6c1da94303bbde1bd72a49ee'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
exe=APP+'/Camera'; new=open('/src/Camera','rb').read(); lib=open('/src/libredwoodcamlog.dylib','rb').read()
st=os.stat(exe); cur=sha(exe)
def put(path, data):
    tmp=path+'.nord2tmp'; open(tmp,'wb').write(data); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
put(APP+'/libredwoodcamlog.dylib', lib); print('CAMLOG_DYLIB', hashlib.sha256(lib).hexdigest()[:12])
if cur==hashlib.sha256(new).hexdigest():
    print('CAMLOG_CAMERA_ALREADY')
else:
    assert cur==STOCK, 'Camera is neither the stock executable nor ours: %s'%cur
    if not os.path.exists(exe+'.pre-camlog'): put(exe+'.pre-camlog', open(exe,'rb').read())
    assert sha(exe+'.pre-camlog')==STOCK
    put(exe, new); print('CAMLOG_CAMERA_PATCHED', sha(exe)[:12], 'mode %o'%(os.stat(exe).st_mode&0o7777))
print('CC_JOB_ADDED camera log dylib')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
