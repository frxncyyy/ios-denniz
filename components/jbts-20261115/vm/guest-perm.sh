set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, time
R='/work/root'
for c in ('B3EF198B-7E81-41EF-8392-DB149557925C','A544C8CB-8C8C-4313-8CEB-8233ED03E7D2'):
    base=R+'/private/var/mobile/Containers/Shared/AppGroup/'+c
    for b2,ds,fs in os.walk(base):
        for n in [b2]+[os.path.join(b2,x) for x in ds+fs]:
            st=os.lstat(n); print('PERM', c[:8], n[len(base):] or '/', oct(st.st_mode), st.st_uid, st.st_gid, time.strftime('%m-%d %H:%M', time.gmtime(st.st_mtime)))
        if b2.count('/')-base.count('/')>=2: ds[:]=[]
st=os.stat(R+'/private/var/mobile/Containers/Shared/AppGroup'); print('PERM AppGroup', oct(st.st_mode), st.st_uid)
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
