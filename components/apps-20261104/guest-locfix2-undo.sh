set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-locfix2.sh (also undoes locfix1): the original locationd back from /private/var/db/nord2-locfix/locationd.orig, that dir removed
import os, json, hashlib, shutil
R='/work/root'; P='/usr/libexec/locationd'; K='/private/var/db/nord2-locfix'; BAK=K+'/locationd.orig'
ORIG='75066240cfcc9792b2314bc90847811d164cf702756890078bbd42131f9ef4b9'
sha=lambda p: hashlib.sha256(open(R+p,'rb').read()).hexdigest()
if sha(P)==ORIG:
    if os.path.isdir(R+K): shutil.rmtree(R+K)
    print('LOCFIX2_UNDO_ALREADY original in place')
else:
    assert os.path.isfile(R+BAK) and sha(BAK)==ORIG, 'no verified original to restore: review'
    st=os.stat(R+P); data=open(R+BAK,'rb').read(); tmp=R+P+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,R+P)
    assert sha(P)==ORIG; shutil.rmtree(R+K); print('LOCFIX2_UNDONE original restored', ORIG[:12])
print('CC_JOB_ADDED undo locfix2'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
