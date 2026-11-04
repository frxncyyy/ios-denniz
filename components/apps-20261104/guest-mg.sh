set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os
R='/work/root'
p=R+'/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist'
print('exists', os.path.exists(p))
if os.path.exists(p): os.replace(p, p+'.pre0277k')
print('CC_JOB_ADDED mg cache moved', not os.path.exists(p), os.path.exists(p+'.pre0277k'))
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
