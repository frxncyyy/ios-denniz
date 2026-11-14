set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of webxr1: Nord2Metal + libredwoodair.dylib put back exactly as the receipt record 'webxr1' says (previous = shader99b
# fe4ecc9f + 7c0c5aba), from the backups *.pre-webxr1 or -- only if a backup is missing -- from the copy shipped in this payload dir
# when it is exactly that previous file. Backups and record removed; the switch no-webxr with marker 'made-by-webxr1' removed, a foreign
# one kept. shader99b's record/backups are untouched.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: '36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d', L: '9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a'}
OLD={N: {'fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d': 'shader99b (y2s99b-shaders merged with api99a)'},
     L: {'7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56': 'shader99b library (y2s99b-shaders)'}}
FLAGS=('no-webxr',)
MARK=b'made-by-webxr1\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-webxr1' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        fb[f]=open(S+f,'rb').read(); assert hashlib.sha256(fb[f]).hexdigest() in OLD[f], 'payload fallback %s is not the shader99b state'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('webxr1'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if all(cur[f]!=NEW[f] for f in (N, L)):
    for f in (N, L):
        assert cur[f] in OLD[f], 'the installed %s is neither webxr1 nor shader99b: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
    if rec: assert rec.get('previous')==cur, 'a record webxr1 is there but the bundle is not its previous state: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('stale backup %s removed'%f)
    if rec:
        rc.pop('webxr1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record webxr1 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('WEBXR1_UNDONE', did or 'nothing to do (webxr1 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'webxr1 files are installed without a matching record webxr1: review'
    src={}
    for f in (N, L):
        prev=rec['previous'][f]; assert prev in OLD[f], 'the record names an unknown previous %s: %s'%(f, prev)
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in (NEW[f], prev), 'the installed %s is neither webxr1 nor its previous file: review'%f
        if os.path.exists(BAK[f]) and sha(BAK[f])==prev: src[f]=(open(BAK[f],'rb').read(), 'backup')
        elif not os.path.exists(BAK[f]) and f in fb and hashlib.sha256(fb[f]).hexdigest()==prev: src[f]=(fb[f], 'fallback in the undo payload')
        elif cur[f]==prev: src[f]=None
        else: raise AssertionError('no usable copy of the previous %s (%s)'%(f, prev))
        assert not os.path.exists(B+'/'+f+'.nord2tmp'), 'leftover .nord2tmp'
    assert not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: Nord2Metal, library, receipt, backups ----
    for f in (N, L):
        if src[f] is None: continue
        put(B+'/'+f, src[f][0], B+'/'+f); inv[f]=sha(B+'/'+f); assert inv[f]==rec['previous'][f]
        did.append('%s restored to %s %s (%s)'%(f, OLD[f][inv[f]], inv[f][:12], src[f][1]))
    rc['inventory'][REL]=inv; rc.pop('webxr1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('backup %s removed'%f)
    print('WEBXR1_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record webxr1 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED webxr1 removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('WEBXR1_FLAG', fl, 'removed')
print('WEBXR1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
