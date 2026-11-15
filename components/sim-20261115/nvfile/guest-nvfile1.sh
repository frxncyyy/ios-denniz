set +e
# nvfile1 (gpu/build/sim-20261115/nvfile): puts THIS phone's modem NVRAM blob (N2NVDSK1, the one its modem booted from until the raw
# copy at userdata+64 GiB was overwritten by HFS free-space reuse) into the root volume as /private/var/db/nord2-modem/n2nvdisk.bin
# (0600 root:wheel), where kernel piece 0715 looks for it first. HFS owns those blocks: never overwritten again. Header/table/CRC
# checked here exactly as the kernel does. Idempotent (NVFILE1_ALREADY); refuses a different existing file unless plan 'replace'.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, struct, zlib
R='/work/root'; D='/private/var/db/nord2-modem'; F=D+'/n2nvdisk.bin'; REC=D+'/nvfile1.json'
plan=json.load(open('/src/plan.json')); b=open('/src/n2nvdisk.bin','rb').read(); sha=hashlib.sha256(b).hexdigest()
assert sha==plan['blob_sha256'], 'NVFILE1_REFUSED payload blob is not the planned one'
ver,count,tbytes,tcrc=struct.unpack_from('<IIII',b,8); h=bytearray(b[:64]); hc=struct.unpack_from('<I',b,32)[0]; struct.pack_into('<I',h,32,0)
assert b[:8]==b'N2NVDSK1' and ver==1 and 0<count<=8192 and tbytes==count*128 and zlib.crc32(bytes(h))==hc and zlib.crc32(b[4096:4096+tbytes])==tcrc, 'NVFILE1_REFUSED blob header/table invalid'
if os.path.exists(R+F):
    cur=hashlib.sha256(open(R+F,'rb').read()).hexdigest()
    if cur==sha: print('NVFILE1_ALREADY', sha[:12], count, 'files'); raise SystemExit(0)
    assert plan.get('replace'), 'NVFILE1_REFUSED %s exists with another blob (%s): use the -replace variant'%(F,cur[:12])
made_dir=not os.path.isdir(R+D)
if made_dir: os.makedirs(R+D); os.chmod(R+D,0o755); os.chown(R+D,0,0)
tmp=R+F+'.nord2tmp'
with open(tmp,'wb') as f: f.write(b); f.flush(); os.fsync(f.fileno())
os.chmod(tmp,0o600); os.chown(tmp,0,0); os.replace(tmp,R+F)
assert hashlib.sha256(open(R+F,'rb').read()).hexdigest()==sha
old=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
rec={'payload':'nvfile1','blob_sha256':sha,'files':count,'made_dir':old.get('made_dir',made_dir)}
open(R+REC,'w').write(json.dumps(rec,indent=1)+'\n'); os.chmod(R+REC,0o644); os.chown(R+REC,0,0)
print('NVFILE1_INSTALLED', sha[:12], count, 'files')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'NVFILE1_OK'
sync; umount /work/root
echo 'CC_JOB_ADDED nvfile (modem NVRAM blob file; no launchd job)'
echo 'CC_RECEIPT_UPDATED nvfile (n2nvdisk.bin owned file)'
fsck.hfsplus -f -n /dev/vda; echo INSTALL_DONE
