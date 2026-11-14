set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# camvdoquarter (camprev-20261114): test: a 4:3 video data output of more than 6 MP made a quarter as wide (1024x768) instead of half (2048x1536) by the camera bundle of q10jcam1. One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-vdo-quarter'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOQUARTER_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOQUARTER_UNDO removed', P)
    else: print('CAMVDOQUARTER_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOQUARTER_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOQUARTER_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdoquarter (camprev-20261114): test: a 4:3 video data output of more than 6 MP made a quarter as wide (1024x768) instead of half (2048x1536) by the camera bundle of q10jcam1. One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-vdo-quarter'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOQUARTER_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOQUARTER_UNDO removed', P)
    else: print('CAMVDOQUARTER_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOQUARTER_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOQUARTER_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdoquarter (camprev-20261114): test: a 4:3 video data output of more than 6 MP made a quarter as wide (1024x768) instead of half (2048x1536) by the camera bundle of q10jcam1. One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-vdo-quarter'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOQUARTER_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOQUARTER_UNDO removed', P)
    else: print('CAMVDOQUARTER_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOQUARTER_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOQUARTER_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camvdoquarter (camprev-20261114): test: a 4:3 video data output of more than 6 MP made a quarter as wide (1024x768) instead of half (2048x1536) by the camera bundle of q10jcam1. One empty switch file, read by the bundle once per mediaserverd (restart or reboot to change).
import os
R='/work/root'; P='/private/var/db/nord2-camera-vdo-quarter'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMVDOQUARTER_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMVDOQUARTER_UNDO removed', P)
    else: print('CAMVDOQUARTER_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMVDOQUARTER_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMVDOQUARTER_INSTALL created', P)
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
