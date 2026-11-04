set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# Undo of the switch of guest-vcodec-vtalign64.sh: removes /private/var/db/nord2-metal/vt-alignment-64 only when it carries the
# marker that script writes. Nothing else changes (the prod8 files stay installed).
import os
R='/work/root'; g='/private/var/db/nord2-metal/vt-alignment-64'; MARK=b'nord2 vtalign64 switch (guest-vcodec-vtalign64.sh)\n'
if not os.path.lexists(R+g):
    print('VTALIGN64_UNDO nothing to remove'); print('CC_JOB_ADDED vtalign64 switch already absent'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert os.path.isfile(R+g) and not os.path.islink(R+g), g
    assert open(R+g,'rb').read()==MARK, 'the switch file was not made by guest-vcodec-vtalign64.sh: not removing it'
    os.remove(R+g); print('VTALIGN64_UNDO removed',g); print('CC_JOB_ADDED vtalign64 switch removed'); print('CC_RECEIPT_UPDATED unchanged')
print('VTALIGN64_STATE',[n for n in sorted(os.listdir(R+'/private/var/db/nord2-metal')) if not n.startswith(('Nord2Metal.pre','libredwoodair'))])
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
