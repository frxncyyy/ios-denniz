set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os
R='/work/root'
H='0'*96
base=R+'/'+H+'/usr/local/standalone/firmware/Baseband'
os.makedirs(base, exist_ok=True)
for d in [R+'/'+H, R+'/'+H+'/usr', R+'/'+H+'/usr/local', R+'/'+H+'/usr/local/standalone', R+'/'+H+'/usr/local/standalone/firmware', base]:
    os.chmod(d, 0o755)
link=base+'/Mav17'
if os.path.islink(link) or os.path.exists(link): os.remove(link)
os.symlink('/usr/standalone/firmware/Baseband/Mav17', link)
assert os.path.isdir(R+'/usr/standalone/firmware/Baseband/Mav17')
print('CC_JOB_ADDED preboot dir', os.readlink(link))
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
