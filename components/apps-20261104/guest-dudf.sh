set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
echo DUDF_DF; df -m /work/root | tail -1
echo DUDF_TOP; du -xm -d 2 /work/root 2>/dev/null | sort -rn | head -40
echo DUDF_VAR; du -xm -d 3 /work/root/private/var 2>/dev/null | sort -rn | head -40
echo CC_JOB_ADDED none
echo CC_RECEIPT_UPDATED unchanged
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
