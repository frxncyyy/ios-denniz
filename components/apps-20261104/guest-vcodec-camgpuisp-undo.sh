set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-vcodec-camgpuisp-undo.sh: removes /private/var/db/nord2-camera-gpu-isp if guest-vcodec-camgpuisp.sh made it (marker). Nothing else.
import os
R='/work/root'; g='/private/var/db/nord2-camera-gpu-isp'; MARK=b'nord2 camgpuisp switch (guest-vcodec-camgpuisp.sh)\n'
if not os.path.lexists(R+g):
    print('CAMGPUISP_UNDO nothing to remove'); print('CC_JOB_ADDED camgpuisp switch already absent'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert os.path.isfile(R+g) and not os.path.islink(R+g), g
    assert open(R+g,'rb').read()==MARK, 'the switch file was not made by guest-vcodec-camgpuisp.sh: not removing it'
    os.remove(R+g); print('CAMGPUISP_UNDO removed',g); print('CC_JOB_ADDED camgpuisp switch removed'); print('CC_RECEIPT_UPDATED unchanged')
print('CAMGPUISP_STATE',sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-camera-')))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
