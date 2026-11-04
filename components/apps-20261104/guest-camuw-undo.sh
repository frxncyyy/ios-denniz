set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, os
R='/work/root'; dst=R+'/usr/lib/libredwoodcamui.dylib'; prev=dst+'.prev'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
for p in (dst, prev):
    assert not os.path.islink(p) and not os.path.isdir(p), 'something else is at '+p
    if os.path.exists(p):
        assert b'/usr/lib/libredwoodcamui.dylib\0' in open(p,'rb').read(), p+' is not our dylib: left alone'
for p in (dst, prev):
    if os.path.exists(p): h=sha(p); os.unlink(p); print('CAMUW_REMOVED', os.path.basename(p), h[:12])
    else: print('CAMUW_NOT_THERE', os.path.basename(p))
assert not os.path.exists(dst) and not os.path.exists(prev)
print('CC_JOB_ADDED camera: /usr/lib/libredwoodcamui.dylib removed')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
