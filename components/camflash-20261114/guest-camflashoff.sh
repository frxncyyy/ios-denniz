set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# camflashoff (camflash-20261114): kill switch: the camera bundle never fires the flash (photographs as before 0560).  One empty switch file read by the camera bundle (audio.pkg of q10jcr) at each photograph.
import os
R='/work/root'; P='/private/var/db/nord2-camera-noflash'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMFLASHOFF_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMFLASHOFF_UNDO removed', P)
    else: print('CAMFLASHOFF_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMFLASHOFF_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMFLASHOFF_INSTALL created', P)
os.sync()
GUEST
echo 'CC_JOB_ADDED camflashoff'
echo 'CC_RECEIPT_UPDATED unchanged'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
