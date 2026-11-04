set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os
R='/work/root'
C=R+'/private/var/containers/Data/System/F4D1291A-41CD-47A3-924C-6A10AF7194D6/Library/Caches'
for n in ('libraries.data','libraries.list'):
    p=C+'/'+n
    if os.path.exists(p):
        os.replace(p, p+'.bad-q10ee')
        print('CC_JOB_ADDED set aside', n)
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
