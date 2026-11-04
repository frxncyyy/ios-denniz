set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-vcodec-camzc-undo.sh: removes /private/var/db/nord2-camera-{gpu-isp,zerocopy} where guest-vcodec-camzc.sh made them (marker). Nothing else.
import os
R='/work/root'; MARK=b'nord2 camzc switch (guest-vcodec-camzc.sh)\n'
for g in ('/private/var/db/nord2-camera-gpu-isp', '/private/var/db/nord2-camera-zerocopy'):
    if not os.path.lexists(R+g):
        print('CAMZC_UNDO nothing to remove', g)
    else:
        assert os.path.isfile(R+g) and not os.path.islink(R+g), g
        assert open(R+g,'rb').read()==MARK, 'the switch file was not made by guest-vcodec-camzc.sh: not removing it: '+g
        os.remove(R+g); print('CAMZC_UNDO removed',g)
print('CC_JOB_ADDED camzc switches handled'); print('CC_RECEIPT_UPDATED unchanged')
print('CAMZC_STATE',sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-camera-')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
