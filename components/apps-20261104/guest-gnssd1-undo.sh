set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# undo of guest-gnssd1.sh: job removed (Metal receipt updated); binary, gate, receipt removed; /private/var/db/n2gnss removed
# if gnssd1 created it (logs included); the gnssstep gate restored if gnssd1 moved it.
import os, json, hashlib, plistlib, shutil
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2mnld'; DIR='/private/var/db/n2gnss'; GATE='/private/var/db/nord2-gnssd'; REC='/private/var/db/nord2-gnssd.json'
SGATE='/private/var/db/nord2-gnssstep'; SOFF=SGATE+'.off-gnssd1'
KEY='/System/Library/LaunchDaemons/com.nord2.gnssd.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'launchd.plist differs from the Metal receipt: not touching it'
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
if KEY in cfg['LaunchDaemons']:
    del cfg['LaunchDaemons'][KEY]; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('GNSSD1_UNDO job removed')
if os.path.exists(R+SOFF) and (rec.get('gnssstep_gate_moved') or not rec):
    if not os.path.exists(R+SGATE): os.rename(R+SOFF, R+SGATE); print('GNSSD1_UNDO gnssstep gate restored', SGATE)
    else: os.remove(R+SOFF)
for p in (GATE, BIN):
    if os.path.lexists(R+p): os.remove(R+p); print('GNSSD1_UNDO removed', p)
if os.path.isdir(R+DIR) and rec.get('dir_made', True): shutil.rmtree(R+DIR); print('GNSSD1_UNDO removed', DIR)
if os.path.exists(R+REC): os.remove(R+REC); print('GNSSD1_UNDO removed', REC)
print('CC_JOB_ADDED undo gnssd1'); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
