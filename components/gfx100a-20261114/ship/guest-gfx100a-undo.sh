set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, shutil
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gfx100a: Nord2Metal put back exactly as the receipt record 'gfx100a' says (previous = webxr1 36f755ea), from the backup
# Nord2Metal.pre-gfx100a or -- only if it is missing -- from the copy shipped in this payload dir when it is exactly that file. Backup
# and record removed; the log directory private/var/db/nord2-metal/log removed WITH ITS LOGS only when the record says gfx100a made it
# (pull the logs first if they are wanted); switches carrying 'made-by-gfx100a' removed, foreign ones kept. webxr1's record and
# backups are untouched.
N='Nord2Metal'; S='/src/'
NEW='e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593'
OLD={'36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d': 'webxr1 (webxr-20261114 on shader99b)'}
FLAGS=('no-gfx100a', 'no-astcedge', 'no-filelog', 'no-slotdiag', 'no-rtcopy')
MARK=b'made-by-gfx100a\n'
LOGD=DB+'/log'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def inventory(d):
    out={}
    for base,dirs,files in os.walk(d):
        for n in dirs+files:
            p=os.path.join(base,n); assert not os.path.islink(p) and (os.path.isdir(p) or os.path.isfile(p)), 'non-regular entry '+p
            out[os.path.relpath(p,d)]='directory' if os.path.isdir(p) else sha(p)
    return out
def put(path, data, like):
    st=os.stat(like); tmp=path+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
def droplog(rec, did):
    if rec and rec.get('logdir_made') and os.path.lexists(LOGD):
        assert os.path.isdir(LOGD) and not os.path.islink(LOGD), 'log path is not a directory'
        for base,dirs,files in os.walk(LOGD):
            for n in dirs+files: assert not os.path.islink(os.path.join(base,n)), 'a link inside the log directory: review'
        k=len(os.listdir(LOGD)); shutil.rmtree(LOGD); did.append('log directory removed (%d entries)'%k)
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-gfx100a'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb=None
if os.path.exists(S+N):
    fb=open(S+N,'rb').read(); assert hashlib.sha256(fb).hexdigest() in OLD, 'payload fallback Nord2Metal is not the webxr1 one'
cur=sha(B+'/'+N); rec=rc.get('gfx100a'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur!=NEW:
    assert cur in OLD, 'the installed Nord2Metal is neither gfx100a nor webxr1: %s'%cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'unexpected backup '+BAK
    if rec: assert rec.get('previous')==cur, 'a record gfx100a is there but the bundle is not its previous state: review'
    if os.path.exists(BAK): os.unlink(BAK); did.append('stale backup removed')
    droplog(rec, did)
    if rec:
        rc.pop('gfx100a'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gfx100a removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GFX100A_UNDONE', did or 'nothing to do (gfx100a not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the gfx100a Nord2Metal is installed without a matching record gfx100a: review'
    prev=rec['previous']; assert prev in OLD, 'the record names an unknown previous Nord2Metal: %s'%prev
    assert inv[N]==cur, 'receipt/bundle mismatch for Nord2Metal'
    if os.path.exists(BAK) and sha(BAK)==prev: src=(open(BAK,'rb').read(), 'backup')
    elif not os.path.exists(BAK) and fb is not None and hashlib.sha256(fb).hexdigest()==prev: src=(fb, 'fallback in the undo payload')
    else: raise AssertionError('no usable copy of the previous Nord2Metal (%s)'%prev)
    assert not os.path.exists(B+'/'+N+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    if os.path.lexists(LOGD): assert os.path.isdir(LOGD) and not os.path.islink(LOGD), 'log path is not a directory'
    # ---- writes: Nord2Metal, receipt, backup, log directory ----
    put(B+'/'+N, src[0], B+'/'+N); inv[N]=sha(B+'/'+N); assert inv[N]==prev
    did.append('Nord2Metal restored to %s %s (%s)'%(OLD[prev], prev[:12], src[1]))
    rc['inventory'][REL]=inv; rc.pop('gfx100a'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    if os.path.exists(BAK): os.unlink(BAK); did.append('backup removed')
    droplog(rec, did)
    print('GFX100A_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {N: inv[N][:12]}, 'record gfx100a removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gfx100a removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GFX100A_FLAG', fl, 'removed')
print('GFX100A_BUNDLE', {N: sha(B+'/'+N)[:12]}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
