set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# mdpoff (mdp-20261114): kill switch for 0671/0672: the kernel runs only the MDP probe (no DMA self-test, no AppleM2ScalerCSCDriver). One empty switch file, read once per boot at +45 s.
import os
R='/work/root'; P='/private/var/db/nord2-mdp-off'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('MDPOFF_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('MDPOFF_UNDO removed', P)
    else: print('MDPOFF_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('MDPOFF_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('MDPOFF_INSTALL created', P)
os.sync()
GUEST
echo 'CC_JOB_ADDED mdpoff-undo'
echo 'CC_RECEIPT_UPDATED unchanged'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
