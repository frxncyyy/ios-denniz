set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-ispfeat-undo.sh (any variant): removes /private/var/db/nord2-isp-features only where guest-ispfeat-*.sh made it (marker
# /private/var/db/nord2-metal/isp-features.made-by-ispfeat, and the switch still holds the text the marker records), then the marker.
# A switch not made by ispfeat is left as is. Nothing else.
import os
R='/work/root'; G='/private/var/db/nord2-isp-features'; MK='/private/var/db/nord2-metal/isp-features.made-by-ispfeat'
HDR=b'ispfeat created /private/var/db/nord2-isp-features with:\n'
did=[]
if os.path.lexists(R+MK):
    assert os.path.isfile(R+MK) and not os.path.islink(R+MK), 'not a regular file: '+MK
    mine=open(R+MK,'rb').read(); assert mine.startswith(HDR), 'not an ispfeat marker: review'
    if os.path.lexists(R+G):
        have=open(R+G,'rb').read() if os.path.isfile(R+G) and not os.path.islink(R+G) else None
        assert have==mine[len(HDR):], 'the switch is no longer what ispfeat wrote (%r, marker %r): not removing it'%(have and have[:64], mine[len(HDR):][:64])
        os.remove(R+G); did.append('switch %r removed'%have)
    os.remove(R+MK); did.append('marker removed')
elif os.path.lexists(R+G):
    did.append('switch there but NOT made by ispfeat: left as is')
print('ISPFEAT_UNDONE', did or 'nothing to do')
print('CC_JOB_ADDED ispfeat removed'); print('CC_RECEIPT_UPDATED unchanged')
print('ISPFEAT_STATE', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-isp')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
