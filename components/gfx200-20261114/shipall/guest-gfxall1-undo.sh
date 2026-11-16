set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gfxall1: Nord2Metal and libredwoodair.dylib put back exactly as record 'gfxall1' says, from the backups *.pre-gfxall1 or -- only for a
# missing backup -- from the copies in this payload dir (shader99b's pair, the mirror's state) when they are exactly those files.
# Backups and record removed; switches marked 'made-by-gfxall' removed, foreign ones kept. Other records untouched.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593', L: '366b0e1a2448fe51f7b79597360dd93682fcd0c87e6279514118a6a6b2745581'}
OLD={N: {
              "e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593": "gfx100a (on webxr1)",
              "36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d": "webxr1",
              "fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d": "shader99b"
     },
     L: {
              "f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56": "bigpool1x (on webxr1)",
              "9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a": "webxr1",
              "d1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14": "bigpool1 (on shader99b)",
              "7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56": "shader99b",
              "46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8": "gpupipe1 (on bigpool1)"
     }}
FLAGS=('no-gpupipe', 'no-bigpool', 'no-webxr', 'no-gfx100a', 'no-rtcopy')
MARK=b'made-by-gfxall\n'
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfxall1', L: DB+'/'+L+'.pre-gfxall1'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        fb[f]=open(S+f,'rb').read(); assert hashlib.sha256(fb[f]).hexdigest() in OLD[f], 'payload fallback %s is not an accepted previous state'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfxall1'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if not (rec and cur==NEW):
    for f in (N, L):
        assert cur[f] in OLD[f], 'the installed %s is neither gfxall1 nor a state it replaces: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
        if rec: assert rec['previous'][f]==cur[f], 'a record gfxall1 is there but the bundle is not its previous state: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('stale backup %s removed'%f)
    if rec:
        rc.pop('gfxall1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gfxall1 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GFXALL1_UNDONE', did or 'nothing to do (gfxall1 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the gfxall1 files are installed without a matching record gfxall1: review'
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
    rc['inventory'][REL]=inv; rc.pop('gfxall1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('backup %s removed'%f)
    print('GFXALL1_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record gfxall1 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gfxall1 removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GFXALL1_FLAG', fl, 'removed')
print('GFXALL1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
