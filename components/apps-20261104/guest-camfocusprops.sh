set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os
m='/work/root/private/var/db/nord2-camera-focusprops'
assert os.path.isdir(os.path.dirname(m))
open(m,'wb').close(); os.chmod(m,0o644)
print('CAMFOCUS_MARKER', os.path.exists(m))
print('CC_JOB_ADDED camera: the back wide stream lists CMCapture focus properties (exploration; undo: remove the marker)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
