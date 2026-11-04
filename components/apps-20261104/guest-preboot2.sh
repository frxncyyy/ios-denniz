set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, shutil
R='/work/root'; H='0'*96
P=R+'/private/preboot/'+H
base=P+'/usr/local/standalone/firmware/Baseband'
assert os.path.isdir(R+'/private/preboot')
os.makedirs(base, exist_ok=True)
d=P
for part in ['', '/usr', '/usr/local', '/usr/local/standalone', '/usr/local/standalone/firmware', '/usr/local/standalone/firmware/Baseband']:
    os.chmod(P+part, 0o755); os.chown(P+part, 0, 0)
link=base+'/Mav17'
if os.path.lexists(link): os.remove(link)
os.symlink('/usr/standalone/firmware/Baseband/Mav17', link)
assert not os.path.exists(P+'/usr/standalone/firmware') and not os.path.exists(P+'/.roots-installed')
old=R+'/'+H
if os.path.isdir(old) and not os.path.islink(old): shutil.rmtree(old)
print('CC_JOB_ADDED preboot2', os.readlink(link), 'old removed', not os.path.exists(old))
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
