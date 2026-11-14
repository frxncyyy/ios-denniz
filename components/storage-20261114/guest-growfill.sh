set +e
# storage-20261114 REHEARSAL ONLY (never for the phone): allocate past the old 16 GiB end on a grown volume.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
df -k /work/root | tail -1
yes nord2-growfill-A | head -c 629145600 > /work/root/private/var/tmp/nord2-growA; echo FILL_A_RC=$?
yes nord2-growfill-B | head -c 629145600 > /work/root/private/var/tmp/nord2-growB; echo FILL_B_RC=$?
sync; rm -f /work/root/private/var/tmp/nord2-growA; sync
df -k /work/root | tail -1
umount /work/root
echo 'CC_JOB_ADDED growfill'
echo 'CC_RECEIPT_UPDATED none'
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
