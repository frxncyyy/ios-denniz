set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-camzc2.sh: creates the camera zero-copy switch /private/var/db/nord2-camera-zerocopy (empty file, root:wheel 644; q10jn: the readers read
# their frames into the GPU's memory and the viewfinder's/film's pictures are rendered there, no upload). The switch has no content, so the
# marker /private/var/db/nord2-metal/camera-zerocopy.made-by-camzc2 records that we made it; guest-camzc2-undo.sh removes only that.
# An existing switch (e.g. made by guest-vcodec-camzc.sh) is left as is. Nothing else is touched.
import os
R='/work/root'; G='/private/var/db/nord2-camera-zerocopy'; MK='/private/var/db/nord2-metal/camera-zerocopy.made-by-camzc2'
assert os.path.isdir(R+'/private/var/db/nord2-metal'), 'no /private/var/db/nord2-metal'
def mk(path, data):
    with open(R+path,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(R+path,0o644); os.chown(R+path,0,0)
if os.path.lexists(R+G):
    assert os.path.isfile(R+G) and not os.path.islink(R+G), 'not a regular file: '+G
    st=os.stat(R+G)
    print('CAMZC2_SWITCH already there', G, st.st_size, 'bytes', 'mode %o uid %d gid %d'%(st.st_mode&0o7777, st.st_uid, st.st_gid),
          'made by camzc2' if os.path.exists(R+MK) else 'NOT made by camzc2 (left as is)')
    print('CC_JOB_ADDED camzc2 already there'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert not os.path.lexists(R+MK), 'camzc2 marker without the switch: review'
    mk(MK, b'camzc2 created /private/var/db/nord2-camera-zerocopy\n'); mk(G, b'')
    print('CAMZC2_SWITCH on', G, 'marker', MK)
    print('CC_JOB_ADDED camzc2: camera zero-copy switch'); print('CC_RECEIPT_UPDATED unchanged (no addon receipt change)')
st=os.stat(R+G); print('CAMZC2_STATE', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-camera-')), 'zerocopy mode %o %d:%d %d bytes'%(st.st_mode&0o7777, st.st_uid, st.st_gid, st.st_size))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
