set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, hashlib, shutil
R='/work/root'
P=R+'/System/Library/PrivateFrameworks/TelephonyUtilities.framework/callservicesd'
old=hashlib.sha256(open(P,'rb').read()).hexdigest()
print('old', old[:16])
assert old=='86db594cbfcad8ef9c507d6d795b6fabb1c1ce006ca38a9062cf7bd01e029aeb', 'unexpected callservicesd (not 0277u)'
new=open('/src/callservicesd','rb').read()
assert hashlib.sha256(new).hexdigest()=='408f228d54d245d1a6ec566ab1b157091b5fab60a7c0dece453389b07ff840ae'
st=os.stat(P)
shutil.copy2(P, P+'.pre0283')
tmp=P+'.nord2tmp'; open(tmp,'wb').write(new); os.chmod(tmp, st.st_mode&0o7777); os.chown(tmp, st.st_uid, st.st_gid); os.replace(tmp, P)
print('CC_JOB_ADDED callservicesd replaced', hashlib.sha256(open(P,'rb').read()).hexdigest()[:16])
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
