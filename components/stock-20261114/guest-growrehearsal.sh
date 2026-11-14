set +e
# VM-only rehearsal of hfs_grow.py on a qcow2 overlay (virtual size = userdata partition) of the rootfs mirror.
# Never used on the phone. grow -> fsck -> undo -> fsck + byte compare -> grow -> fsck -> mount, fill past 16 GiB -> fsck.
P=115024560128; mkdir -p /tmp
dd if=/dev/vda bs=1048576 count=1 status=none | sha256sum > /tmp/c0.before
dd if=/dev/vda bs=1048576 skip=16383 count=1 status=none | sha256sum > /tmp/c16383.before
python3 /src/hfs_grow.py apply-image /dev/vda --part-bytes $P --receipt /tmp/grow.json; echo GROW1_RC=$?
fsck.hfsplus -f -n /dev/vda | tail -3
python3 /src/hfs_grow.py undo-image /dev/vda --part-bytes $P --receipt /tmp/grow.json; echo UNDO_RC=$?
fsck.hfsplus -f -n /dev/vda | tail -2
dd if=/dev/vda bs=1048576 count=1 status=none | sha256sum | cmp - /tmp/c0.before && echo UNDO_CHUNK0_IDENTICAL
dd if=/dev/vda bs=1048576 skip=16383 count=1 status=none | sha256sum | cmp - /tmp/c16383.before && echo UNDO_CHUNK16383_IDENTICAL
python3 /src/hfs_grow.py apply-image /dev/vda --part-bytes $P --receipt /tmp/grow2.json; echo GROW2_RC=$?
fsck.hfsplus -f -n /dev/vda | tail -3
mount -t hfsplus -o rw,force /dev/vda /work/root && df -k /work/root
dd if=/dev/zero of=/work/root/private/var/tmp/nord2-growfill bs=1048576 count=1300 status=none; echo FILL_RC=$?
df -k /work/root; sync; umount /work/root
python3 - <<'PY'
import struct
f=open('/dev/vda','rb'); f.seek(1024); v=f.read(512)
print('VH total',struct.unpack_from('>I',v,44)[0],'free',struct.unpack_from('>I',v,48)[0],'attrs',hex(struct.unpack_from('>I',v,4)[0]))
PY
fsck.hfsplus -f -n /dev/vda | tail -3
python3 /src/hfs_grow.py undo-image /dev/vda --part-bytes $P --receipt /tmp/grow2.json; echo UNDO_AFTER_USE_RC=$?
echo CC_JOB_ADDED growrehearsal; echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda | tail -1
echo INSTALL_DONE
