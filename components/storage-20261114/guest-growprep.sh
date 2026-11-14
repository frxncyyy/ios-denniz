set +e
# storage-20261114 growprep: no payload. After the journal replay + fsck -y (NEW_FSCK, prepended by generic_update_offline.py), mount rw and
# unmount with Linux hfsplus (sets the 'cleanly unmounted' bit, journal stays empty) so commit_hfs_grow.py's preconditions hold on the device
# after the delta commit. Prints GROWPREP_READY (16 GiB, clean, empty journal) / GROWPREP_ALREADY_GROWN / GROWPREP_REFUSED <reason>.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || { echo "GROWPREP_REFUSED mount failed"; exit 1; }
df -k /work/root | tail -1
sync; umount /work/root || { echo "GROWPREP_REFUSED umount failed"; exit 1; }
python3 - <<'PY'
import struct, json, os
fd = os.open('/dev/vda', os.O_RDONLY); dev = os.lseek(fd, 0, 2)
vh = os.pread(fd, 512, 1024)
u = lambda o: struct.unpack_from('>I', vh, o)[0]
attrs, jib, bs, total, free = u(4), u(12), u(40), u(44), u(48)
ext = [struct.unpack_from('>II', vh, 128 + 8 * i) for i in range(8)]
ji = os.pread(fd, 52, jib * bs); joff, jlen = struct.unpack_from('>QQ', ji, 36)
magic, endian, start, end, size = struct.unpack_from('<IIQQQ', os.pread(fd, 32, joff))
alt = os.pread(fd, 512, dev - 1024)
st = dict(sig=vh[:2].decode(), attrs=hex(attrs), total_blocks=total, free_blocks=free, volume_bytes=total * bs, device_bytes=dev,
          journal=dict(offset=joff, size=jlen, start=start, end=end, empty=start == end), alloc_extents=[e for e in ext if e[1]],
          alt_vh_matches_total=alt[:2] == vh[:2] and struct.unpack_from('>I', alt, 44)[0] == total)
print('GROWPREP_STATE ' + json.dumps(st))
why = []
if not attrs & 0x100: why.append('not cleanly unmounted')
if attrs & 0x800: why.append('inconsistent bit set')
if attrs & 0x2000 and start != end: why.append('journal not empty')
if len([e for e in ext if e[1]]) >= 8: why.append('no free allocation-file extent slot')
if not st['alt_vh_matches_total']: why.append('alternate VH missing/mismatch at the volume end')
if total * bs > 16 << 30: print('GROWPREP_ALREADY_GROWN %d blocks' % total)
elif why: print('GROWPREP_REFUSED ' + '; '.join(why))
else: print('GROWPREP_READY volume clean, journal empty, %d blocks, %d free' % (total, free))
PY
echo 'CC_JOB_ADDED growprep'
echo 'CC_RECEIPT_UPDATED none'
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
