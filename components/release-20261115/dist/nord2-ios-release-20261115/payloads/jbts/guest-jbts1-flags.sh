set +e
# jbts1 kill switches from the PC (payload-jbts1-off|-on|-sshoff|-sshon): files in /private/var/jb/etc/nord2-jb read at every boot by
# jbinit.sh / sshd-run.sh: 'disabled' (whole jbts1 boot side off) and 'ssh-disabled' (SSH server off). Nothing else is touched.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import json, os
R='/work/root'; D=R+'/private/var/jb/etc/nord2-jb'
plan=json.load(open('/src/plan.json')); assert plan['mode']=='flags'
assert os.path.exists(R+'/private/var/db/nord2-jbts.json') and os.path.isdir(D), 'JBTS1_FLAGS_REFUSED jbts1 is not installed'
for f in plan['flags_set']:
    if not os.path.exists(D+'/'+f):
        with open(D+'/'+f,'w') as o: o.write('1\n')
        os.chown(D+'/'+f,0,0); os.chmod(D+'/'+f,0o644)
for f in plan['flags_clear']:
    if os.path.lexists(D+'/'+f): os.remove(D+'/'+f)
print('JBTS1_FLAGS now', sorted(os.listdir(D)))
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'JBTS1_FLAGS_OK'
echo 'CC_JOB_ADDED jbts1-flags'
echo 'CC_RECEIPT_UPDATED none'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
