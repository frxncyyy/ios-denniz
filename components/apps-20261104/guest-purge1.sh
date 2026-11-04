set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, os
R='/work/root'
n=0; b=0
for pat in (R+'/private/var/mobile/Media/DCIM/*/*.MOV', R+'/private/var/mobile/Media/DCIM/*/*.mov', R+'/private/var/db/nord2-vcodec/out/*'):
    for p in glob.glob(pat):
        if os.path.isfile(p):
            b+=os.path.getsize(p); os.remove(p); n+=1
print('PURGE1_REMOVED files=%d bytes=%d'%(n,b))
print('CC_JOB_ADDED storage: test movies and codec outputs removed')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
