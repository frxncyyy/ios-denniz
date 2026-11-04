set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# guest-ispnoexpand.sh: creates the libmerge6 kill switch /private/var/db/nord2-isp-noexpand (empty file, root:wheel 644; libmerge6 checks it
# once, at the first rwa_isp_create: while it exists the ISP pass runs over every pixel, as libmerge5, instead of once per sensor place shown).
# The switch has no content, so the marker /private/var/db/nord2-metal/isp-noexpand.made-by-ispnoexpand records that we made it;
# guest-ispnoexpand-undo.sh removes only that. An existing switch is left as is. Nothing else is touched.
import os
R='/work/root'; G='/private/var/db/nord2-isp-noexpand'; MK='/private/var/db/nord2-metal/isp-noexpand.made-by-ispnoexpand'
assert os.path.isdir(R+'/private/var/db/nord2-metal'), 'no /private/var/db/nord2-metal'
def mk(path, data):
    with open(R+path,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(R+path,0o644); os.chown(R+path,0,0)
if os.path.lexists(R+G):
    assert os.path.isfile(R+G) and not os.path.islink(R+G), 'not a regular file: '+G
    st=os.stat(R+G)
    print('ISPNOEXPAND_SWITCH already there', G, st.st_size, 'bytes', 'mode %o uid %d gid %d'%(st.st_mode&0o7777, st.st_uid, st.st_gid),
          'made by ispnoexpand' if os.path.exists(R+MK) else 'NOT made by ispnoexpand (left as is)')
    print('CC_JOB_ADDED ispnoexpand already there'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert not os.path.lexists(R+MK), 'ispnoexpand marker without the switch: review'
    mk(MK, b'ispnoexpand created /private/var/db/nord2-isp-noexpand\n'); mk(G, b'')
    print('ISPNOEXPAND_SWITCH on', G, 'marker', MK)
    print('CC_JOB_ADDED ispnoexpand: libmerge6 ISP expand pass OFF'); print('CC_RECEIPT_UPDATED unchanged (no addon receipt change)')
st=os.stat(R+G); print('ISPNOEXPAND_STATE', sorted(n for n in os.listdir(R+'/private/var/db') if n.startswith('nord2-isp')), 'noexpand mode %o %d:%d %d bytes'%(st.st_mode&0o7777, st.st_uid, st.st_gid, st.st_size))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
