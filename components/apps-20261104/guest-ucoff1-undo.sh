set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import json, os
R='/work/root'; DB=R+'/private/var/db/nord2-metal'
# undo of ucoff1: removes /var/db/nord2-metal/usecount-off only when it carries the marker 'made-by-ucoff1'; any other is kept.
FL='usecount-off'; MARK=b'made-by-ucoff1\n'
rc=json.load(open(DB+'/receipt.json'))
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
dst=DB+'/'+FL
if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('UCOFF1_UNDONE usecount-off removed')
elif os.path.exists(dst): print('UCOFF1_UNDONE nothing to do: usecount-off present and not ours (%r), kept'%open(dst,'rb').read(64))
else: print('UCOFF1_UNDONE nothing to do (no usecount-off)')
assert not (os.path.exists(dst) and open(dst,'rb').read()==MARK)
print('CC_JOB_ADDED ucoff1 removed')
print('CC_RECEIPT_UPDATED unchanged (switch file only)')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
