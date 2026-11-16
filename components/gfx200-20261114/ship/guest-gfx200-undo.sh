set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gfx200: Nord2Metal and libredwoodair.dylib put back exactly as the receipt record 'gfx200' says, from the backups
# *.pre-gfx200 or -- only for a missing backup -- from the copies shipped in this payload dir when they are exactly those files
# (the payload ships gfx100a's Nord2Metal e6f75db1 and bigpool1x's library f271fcbe). Backups and record removed; switches carrying
# 'made-by-gfx200' removed, foreign ones kept. Other features' records and backups are untouched.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3', L: '35b33aa9367bc73806160e47b8681857d2006a4d079cba4f4cc25633781893f3'}
OLD={N: {'e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593': 'gfx100a (on webxr1)',
         '36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d': 'webxr1',
         'fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d': 'shader99b'},
     L: {'f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56': 'bigpool1x (on webxr1)',
         '9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a': 'webxr1',
         'd1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14': 'bigpool1 (on shader99b)',
         '7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56': 'shader99b'}}
FLAGS=('no-gfx200', 'no-gfx200-msaartt', 'no-gfx200-zsdc', 'no-gfx200-rtpool', 'no-gfx200-pools')
MARK=b'made-by-gfx200\n'
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfx200', L: DB+'/'+L+'.pre-gfx200'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        fb[f]=open(S+f,'rb').read(); assert hashlib.sha256(fb[f]).hexdigest() in OLD[f], 'payload fallback %s is not an accepted previous state'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfx200'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur[N]!=NEW[N] and cur[L]!=NEW[L]:
    for f in (N, L):
        assert cur[f] in OLD[f], 'the installed %s is neither gfx200 nor a state it replaces: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
        if rec: assert rec['previous'][f]==cur[f], 'a record gfx200 is there but the bundle is not its previous state: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('stale backup %s removed'%f)
    if rec:
        rc.pop('gfx200'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gfx200 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GFX200_UNDONE', did or 'nothing to do (gfx200 not installed)')
else:
    # ---- all checks first ----
    assert cur==NEW, 'only one of the two gfx200 files is installed: review (%r)'%cur
    assert rec and rec.get('installed')==NEW, 'the gfx200 files are installed without a matching record gfx200: review'
    prev=rec['previous']; src={}
    for f in (N, L):
        assert prev[f] in OLD[f], 'the record names an unknown previous %s: %s'%(f, prev[f])
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        if os.path.exists(BAK[f]) and sha(BAK[f])==prev[f]: src[f]=(open(BAK[f],'rb').read(), 'backup')
        elif not os.path.exists(BAK[f]) and f in fb and hashlib.sha256(fb[f]).hexdigest()==prev[f]: src[f]=(fb[f], 'fallback in the undo payload')
        else: raise AssertionError('no usable copy of the previous %s (%s)'%(f, prev[f]))
        assert not os.path.exists(B+'/'+f+'.nord2tmp'), 'leftover .nord2tmp'
    assert not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: Nord2Metal, library, receipt, backups ----
    for f in (N, L):
        put(B+'/'+f, src[f][0], B+'/'+f); inv[f]=sha(B+'/'+f); assert inv[f]==prev[f]
        did.append('%s restored to %s %s (%s)'%(f, OLD[f][prev[f]], prev[f][:12], src[f][1]))
    rc['inventory'][REL]=inv; rc.pop('gfx200'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('backup %s removed'%f)
    print('GFX200_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record gfx200 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gfx200 removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GFX200_FLAG', fl, 'removed')
print('GFX200_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
