set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gpupipe1 (gpusched-20261114 libredwoodair.dylib): the library is put back exactly as the receipt record 'gpupipe1' says (previous =
# shader99b's 7c0c5aba), from its backup libredwoodair.dylib.pre-gpupipe1 or -- only if that backup is missing -- from the copy shipped
# in this payload dir when it is exactly that previous file. Nord2Metal is not touched. Backup and record removed; the switch no-gpupipe
# removed if it carries the marker 'made-by-gpupipe1', kept otherwise. shader99b's record/backups are untouched.
L='libredwoodair.dylib'; S='/src/'
NEW={L: '46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8'}
OLD={L: {'7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56': 'shader99b library (y2s99b-shaders)', 'd1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14': 'bigpool1 library (y2s99c-perf)'}}
FLAGS=('no-gpupipe',)
MARK=b'made-by-gpupipe1\n'
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
RP=DB+'/receipt.json'; BAK={L: DB+'/'+L+'.pre-gpupipe1'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
if os.path.exists(S+L):
    fb[L]=open(S+L,'rb').read(); assert hashlib.sha256(fb[L]).hexdigest() in OLD[L], 'payload fallback %s is neither the shader99b nor the bigpool1 library'%L
cur={L: sha(B+'/'+L)}; rec=rc.get('gpupipe1'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur[L]!=NEW[L]:
    assert cur[L] in OLD[L], 'the installed library is neither gpupipe1 nor shader99b/bigpool1: %s'%cur[L]
    if os.path.exists(BAK[L]): assert sha(BAK[L])==cur[L], 'unexpected backup '+BAK[L]
    if rec: assert rec.get('previous')==cur, 'a record gpupipe1 is there but the bundle is not its previous state: review'
    if os.path.exists(BAK[L]): os.unlink(BAK[L]); did.append('stale backup removed')
    if rec:
        rc.pop('gpupipe1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gpupipe1 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GPUPIPE1_UNDONE', did or 'nothing to do (gpupipe1 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the gpupipe1 library is installed without a matching record gpupipe1: review'
    prev=rec['previous'][L]; assert prev in OLD[L], 'the record names an unknown previous library: %s'%prev
    assert inv[L]==cur[L], 'receipt/bundle mismatch for '+L
    if os.path.exists(BAK[L]) and sha(BAK[L])==prev: src=(open(BAK[L],'rb').read(), 'backup')
    elif not os.path.exists(BAK[L]) and L in fb and hashlib.sha256(fb[L]).hexdigest()==prev: src=(fb[L], 'fallback in the undo payload')
    else: raise AssertionError('no usable copy of the previous library (%s)'%prev)
    assert not os.path.exists(B+'/'+L+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: library, receipt, backup ----
    put(B+'/'+L, src[0], B+'/'+L); inv[L]=sha(B+'/'+L); assert inv[L]==prev
    did.append('%s restored to %s %s (%s)'%(L, OLD[L][inv[L]], inv[L][:12], src[1]))
    rc['inventory'][REL]=inv; rc.pop('gpupipe1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    if os.path.exists(BAK[L]): os.unlink(BAK[L]); did.append('backup removed')
    print('GPUPIPE1_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {L: inv[L][:12]}, 'record gpupipe1 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gpupipe1 (gpusched-20261114) removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GPUPIPE1_FLAG', fl, 'removed')
    elif os.path.exists(dst): print('GPUPIPE1_NOTE', fl, 'is present and not ours: kept')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
