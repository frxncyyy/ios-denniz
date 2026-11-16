set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of bigpool1 (y2s99c-perf libredwoodair.dylib): the library is put back exactly as the receipt record 'bigpool1' says (previous =
# webxr1's 9f263e6f), from its backup libredwoodair.dylib.pre-bigpool1 or -- only if that backup is missing -- from the copy shipped
# in this payload dir when it is exactly that previous file. Nord2Metal is not touched. Backup and record removed; the switch no-bigpool
# removed if it carries the marker 'made-by-bigpool1', kept otherwise. webxr1's record/backups are untouched.
L='libredwoodair.dylib'; S='/src/'
NEW={L: 'f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56'}
OLD={L: {'9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a': 'webxr1 library (webxr-20261114)'}}
FLAGS=('no-bigpool',)
MARK=b'made-by-bigpool1\n'
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
RP=DB+'/receipt.json'; BAK={L: DB+'/'+L+'.pre-bigpool1'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
if os.path.exists(S+L):
    fb[L]=open(S+L,'rb').read(); assert hashlib.sha256(fb[L]).hexdigest() in OLD[L], 'payload fallback %s is not the webxr1 library'%L
cur={L: sha(B+'/'+L)}; rec=rc.get('bigpool1'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur[L]!=NEW[L]:
    assert cur[L] in OLD[L], 'the installed library is neither bigpool1 nor webxr1: %s'%cur[L]
    if os.path.exists(BAK[L]): assert sha(BAK[L])==cur[L], 'unexpected backup '+BAK[L]
    if rec: assert rec.get('previous')==cur, 'a record bigpool1 is there but the bundle is not its previous state: review'
    if os.path.exists(BAK[L]): os.unlink(BAK[L]); did.append('stale backup removed')
    if rec:
        rc.pop('bigpool1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record bigpool1 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('BIGPOOL1_UNDONE', did or 'nothing to do (bigpool1 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the bigpool1 library is installed without a matching record bigpool1: review'
    prev=rec['previous'][L]; assert prev in OLD[L], 'the record names an unknown previous library: %s'%prev
    assert inv[L]==cur[L], 'receipt/bundle mismatch for '+L
    if os.path.exists(BAK[L]) and sha(BAK[L])==prev: src=(open(BAK[L],'rb').read(), 'backup')
    elif not os.path.exists(BAK[L]) and L in fb and hashlib.sha256(fb[L]).hexdigest()==prev: src=(fb[L], 'fallback in the undo payload')
    else: raise AssertionError('no usable copy of the previous library (%s)'%prev)
    assert not os.path.exists(B+'/'+L+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: library, receipt, backup ----
    put(B+'/'+L, src[0], B+'/'+L); inv[L]=sha(B+'/'+L); assert inv[L]==prev
    did.append('%s restored to %s %s (%s)'%(L, OLD[L][inv[L]], inv[L][:12], src[1]))
    rc['inventory'][REL]=inv; rc.pop('bigpool1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    if os.path.exists(BAK[L]): os.unlink(BAK[L]); did.append('backup removed')
    print('BIGPOOL1_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {L: inv[L][:12]}, 'record bigpool1 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED bigpool1 (y2s99c-perf) removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('BIGPOOL1_FLAG', fl, 'removed')
    elif os.path.exists(dst): print('BIGPOOL1_NOTE', fl, 'is present and not ours: kept')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
