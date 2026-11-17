set +e
# simcache1 UNDO: removes the job (launchd.plist + Metal receipt sha), /usr/libexec/n2simcache and /private/var/db/nord2-simcache (our
# files: receipt, switches, log, stderr.log). Carrier links the helper removed at run time are NOT put back: CommCenter makes them
# again by itself the next time a SIM is inserted. Nothing installed -> SIMCACHE1_UNDONE (no-op).
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, plistlib
R='/work/root'; BIN='/usr/libexec/n2simcache'; D='/private/var/db/nord2-simcache'; REC=D+'/receipt.json'
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'; JK='/System/Library/LaunchDaemons/com.nord2.simcache.plist'
OURS=['receipt.json','disabled','norestart','keepkey','dryrun','log','stderr.log']
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
raw=rd(LP); cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
if rec is None and JK not in LD and not os.path.lexists(R+BIN):
    print('SIMCACHE1_UNDONE nothing to undo'); raise SystemExit(0)
assert rec is not None, 'SIMCACHE1_REFUSED pieces present but no receipt: not touching them'
if JK in LD:
    rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'SIMCACHE1_REFUSED launchd.plist differs from the Metal receipt'
extra=[n for n in os.listdir(R+D) if n not in OURS] if os.path.isdir(R+D) else []
assert not extra, 'SIMCACHE1_REFUSED unknown files in %s: %r'%(D,extra)
if JK in LD:
    del LD[JK]; fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid)
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR); put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
if os.path.lexists(R+BIN): os.remove(R+BIN)
if os.path.isdir(R+D):
    for n in os.listdir(R+D): os.remove(R+D+'/'+n)
    os.rmdir(R+D)
print('SIMCACHE1_UNDONE removed job, binary, state dir')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
