set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# camvdofull (camprev-20261114): kill switch: the camera bundle of q10jcam1 leaves every video data output at the size asked (as before camprev; Magnifier back to 4096x3072). One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-fullvdo'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOFULL_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOFULL_UNDO removed', P)
    else: print('CAMVDOFULL_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOFULL_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOFULL_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdofull (camprev-20261114): kill switch: the camera bundle of q10jcam1 leaves every video data output at the size asked (as before camprev; Magnifier back to 4096x3072). One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-fullvdo'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOFULL_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOFULL_UNDO removed', P)
    else: print('CAMVDOFULL_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOFULL_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOFULL_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdofull (camprev-20261114): kill switch: the camera bundle of q10jcam1 leaves every video data output at the size asked (as before camprev; Magnifier back to 4096x3072). One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-fullvdo'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOFULL_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOFULL_UNDO removed', P)
    else: print('CAMVDOFULL_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOFULL_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOFULL_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdofull (camprev-20261114): kill switch: the camera bundle of q10jcam1 leaves every video data output at the size asked (as before camprev; Magnifier back to 4096x3072). One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-fullvdo'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOFULL_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOFULL_UNDO removed', P)
    else: print('CAMVDOFULL_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOFULL_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOFULL_INSTALL created', P)
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
