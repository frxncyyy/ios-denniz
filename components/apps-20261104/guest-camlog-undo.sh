set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; APP=R+'/Applications/Camera.app'
STOCK='a0516cafe47507e80aabd60402983b7c279cb00d6c1da94303bbde1bd72a49ee'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
exe=APP+'/Camera'; bak=exe+'.pre-camlog'; lib=APP+'/libredwoodcamlog.dylib'
if sha(exe)!=STOCK:
    assert os.path.exists(bak) and sha(bak)==STOCK, 'no stock backup of Camera'
    st=os.stat(exe); data=open(bak,'rb').read()
    tmp=exe+'.nord2tmp'; open(tmp,'wb').write(data); os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,exe)
assert sha(exe)==STOCK
for p in (bak, lib):
    if os.path.exists(p): os.unlink(p)
print('CAMLOG_UNDONE Camera', sha(exe)[:12], 'mode %o'%(os.stat(exe).st_mode&0o7777), sorted(n for n in os.listdir(APP) if 'camlog' in n))
print('CC_JOB_ADDED camera log dylib removed')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
