set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-ispnoexpand-undo.sh: removes /private/var/db/nord2-isp-noexpand only where guest-ispnoexpand.sh made it (marker
# /private/var/db/nord2-metal/isp-noexpand.made-by-ispnoexpand, and the switch still empty), then the marker. Nothing else.
import os
R='/work/root'; G='/private/var/db/nord2-isp-noexpand'; MK='/private/var/db/nord2-metal/isp-noexpand.made-by-ispnoexpand'
did=[]
if os.path.exists(R+MK):
    if os.path.lexists(R+G):
        assert os.path.isfile(R+G) and not os.path.islink(R+G) and os.path.getsize(R+G)==0, 'the switch is no longer the empty file ispnoexpand made: not removing it: '+G
        os.remove(R+G); did.append('switch removed')
    os.remove(R+MK); did.append('marker removed')
elif os.path.lexists(R+G):
    did.append('switch there but NOT made by ispnoexpand: left as is')
print('ISPNOEXPAND_UNDONE', did or 'nothing to do')
print('CC_JOB_ADDED ispnoexpand removed'); print('CC_RECEIPT_UPDATED unchanged')
print('ISPNOEXPAND_STATE', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-isp')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
