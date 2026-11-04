set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# Removes /private/var/db/nord2-metal/photos-ycbcr-cpu only if guest-photoscpu.sh made it.
import os
R='/work/root'; g='/private/var/db/nord2-metal/photos-ycbcr-cpu'; MARK=b'nord2 photos-ycbcr-cpu switch (guest-photoscpu.sh)\n'
if not os.path.lexists(R+g):
    print('PHOTOSCPU_UNDO nothing to remove')
else:
    assert os.path.isfile(R+g) and not os.path.islink(R+g) and open(R+g,'rb').read()==MARK, 'not made by guest-photoscpu.sh: not removing it'
    os.remove(R+g); print('PHOTOSCPU_UNDO removed',g)
print('CC_JOB_ADDED photos-ycbcr-cpu off'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
