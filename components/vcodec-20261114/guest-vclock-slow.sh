set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
R=/work/root; F=$R/private/var/db/nord2-vcodec/venc-clock
[ -d $R/private/var/db/nord2-vcodec ] || { echo "VCLOCK no vcodec install"; umount /work/root; exit 1; }
if [ slow = slow ]; then printf 'slow\n' > $F.tmp && chmod 644 $F.tmp && chown 0:0 $F.tmp && mv -f $F.tmp $F; echo "VCLOCK set $(cat $F)"
else rm -f $F; echo "VCLOCK removed (auto)"; fi
sync; umount /work/root
fsck.hfsplus -f -n /dev/vda | tail -2
echo INSTALL_DONE
