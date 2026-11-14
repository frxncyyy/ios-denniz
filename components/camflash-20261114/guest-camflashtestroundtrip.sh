set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# camflashtest (camflash-20261114): every back-camera photograph is taken with the flash (a test: CMCapture is not told, the photograph comes from the strobe frame).  One empty switch file read by the camera bundle (audio.pkg of q10jcr) at each photograph.
import os
R='/work/root'; P='/private/var/db/nord2-camera-flash-always'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMFLASHTEST_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMFLASHTEST_UNDO removed', P)
    else: print('CAMFLASHTEST_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMFLASHTEST_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMFLASHTEST_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camflashtest (camflash-20261114): every back-camera photograph is taken with the flash (a test: CMCapture is not told, the photograph comes from the strobe frame).  One empty switch file read by the camera bundle (audio.pkg of q10jcr) at each photograph.
import os
R='/work/root'; P='/private/var/db/nord2-camera-flash-always'; UNDO=False
if not os.path.isdir(R+'/private/var/db'): print('CAMFLASHTEST_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMFLASHTEST_UNDO removed', P)
    else: print('CAMFLASHTEST_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMFLASHTEST_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMFLASHTEST_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camflashtest (camflash-20261114): every back-camera photograph is taken with the flash (a test: CMCapture is not told, the photograph comes from the strobe frame).  One empty switch file read by the camera bundle (audio.pkg of q10jcr) at each photograph.
import os
R='/work/root'; P='/private/var/db/nord2-camera-flash-always'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMFLASHTEST_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMFLASHTEST_UNDO removed', P)
    else: print('CAMFLASHTEST_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMFLASHTEST_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMFLASHTEST_INSTALL created', P)
os.sync()
GUEST
python3 - <<'GUEST'
# camflashtest (camflash-20261114): every back-camera photograph is taken with the flash (a test: CMCapture is not told, the photograph comes from the strobe frame).  One empty switch file read by the camera bundle (audio.pkg of q10jcr) at each photograph.
import os
R='/work/root'; P='/private/var/db/nord2-camera-flash-always'; UNDO=True
if not os.path.isdir(R+'/private/var/db'): print('CAMFLASHTEST_REFUSED no /private/var/db'); raise SystemExit
if UNDO:
    if os.path.lexists(R+P): os.remove(R+P); print('CAMFLASHTEST_UNDO removed', P)
    else: print('CAMFLASHTEST_UNDO nothing to undo')
else:
    if os.path.lexists(R+P): print('CAMFLASHTEST_INSTALL already', P)
    else:
        with open(R+P,'x') as f: f.flush(); os.fsync(f.fileno())
        os.chmod(R+P,0o644); os.chown(R+P,0,0); print('CAMFLASHTEST_INSTALL created', P)
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
