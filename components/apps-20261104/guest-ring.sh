set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, hashlib, shutil
R='/work/root'
P=R+'/System/Library/PrivateFrameworks/TelephonyUtilities.framework/callservicesd'
old=hashlib.sha256(open(P,'rb').read()).hexdigest()
print('old', old[:16])
assert old=='e13090f3d13fe372a7a8ea0e265b7c439d9e4dc04967640298c377b071be1cd7', 'unexpected callservicesd'
new=open('/src/callservicesd','rb').read()
assert hashlib.sha256(new).hexdigest()=='86db594cbfcad8ef9c507d6d795b6fabb1c1ce006ca38a9062cf7bd01e029aeb'
st=os.stat(P)
shutil.copy2(P, P+'.pre0277u')
tmp=P+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp, st.st_mode&0o7777); os.chown(tmp, st.st_uid, st.st_gid); os.replace(tmp, P)
print('CC_JOB_ADDED callservicesd replaced', hashlib.sha256(open(P,'rb').read()).hexdigest()[:16])
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
