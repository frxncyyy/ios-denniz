set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# Creates the Nord2Metal switch /private/var/db/nord2-metal/photos-ycbcr-cpu (with a marker). Nothing else changes.
import os
R='/work/root'; D='/private/var/db/nord2-metal'; g=D+'/photos-ycbcr-cpu'; MARK=b'nord2 photos-ycbcr-cpu switch (guest-photoscpu.sh)\n'
assert os.path.isdir(R+D) and not os.path.islink(R+D), D
if os.path.lexists(R+g):
    assert os.path.isfile(R+g) and not os.path.islink(R+g), g
    print('PHOTOSCPU already there'); print('CC_JOB_ADDED photos-ycbcr-cpu already on'); print('CC_RECEIPT_UPDATED unchanged')
else:
    tmp=R+g+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,0o644); os.chown(tmp,0,0); os.replace(tmp,R+g)
    print('PHOTOSCPU on',g); print('CC_JOB_ADDED photos-ycbcr-cpu on'); print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
