set +e
# nvfile1 UNDO: removes /private/var/db/nord2-modem/n2nvdisk.bin + its record (and the directory if nvfile1 made it and it is empty).
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import json, os
R='/work/root'; D='/private/var/db/nord2-modem'; F=D+'/n2nvdisk.bin'; REC=D+'/nvfile1.json'
if not os.path.exists(R+REC):
    assert not os.path.exists(R+F), 'NVFILE1_REFUSED %s present without our record: not touching it'%F
    print('NVFILE1_UNDONE nothing to undo'); raise SystemExit(0)
rec=json.load(open(R+REC))
if os.path.exists(R+F): os.remove(R+F)
os.remove(R+REC)
if rec.get('made_dir') and not os.listdir(R+D): os.rmdir(R+D)
print('NVFILE1_UNDONE removed'); os.sync()
GUEST
sync; umount /work/root
echo 'CC_JOB_ADDED nvfile (modem NVRAM blob file; no launchd job)'
echo 'CC_RECEIPT_UPDATED nvfile (n2nvdisk.bin owned file)'
fsck.hfsplus -f -n /dev/vda; echo INSTALL_DONE
