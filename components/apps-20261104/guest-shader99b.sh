set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# shader99b = y2s99b-shaders MERGED with api99a (y2s99a-api-gaps: the phone per the mirror of 14 Nov 01:51) + the SHADER-SIDE gaps: translator (sincos/modf/frexp through a
# pointer, fmin3/fmax3/fmedian3, snorm packs, nested undef aggregates, array members of returned structs, buffers without an
# address-space key, the dispatch-sized threadgroup buffer), BLEND COLOUR factors with -setBlendColor, opaque border colours,
# -setThreadgroupMemoryLength: (rwa_dispatch_tg). BOTH files change: the translator and the dispatch live in libredwoodair.dylib.
# Accepted current state: Nord2Metal api99a d86b3f9f or texlayout1 3ae0c2f8, library d9dd236e; their records/backups are left for their
# own undo. Backups Nord2Metal.pre-shader99b / libredwoodair.dylib.pre-shader99b, record 'shader99b' (guest-shader99b-undo.sh).
# Switches (/var/db/nord2-metal/): no-shader99 (all off), no-shader99-xlat, no-blendcolor, no-bordercolor, no-tgmem -- made here only
# when the payload dir ships them (marker 'made-by-shader99b'); a marked one not in the payload is cleared; a foreign one is kept.
# Every check runs before the first write; writes are backups -> library -> Nord2Metal -> receipt.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d', L: '7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56'}
OLD={N: {'d86b3f9f9b731e35fc594d78e11de5efb9ba5793b24d19f3400e1357be163363': 'api99a (y2s99a-api-gaps)', '3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d': 'texlayout1 (y2s98-texlayout)'},
     L: {'d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb': 'gpufinal6 library (y2s96-final)'}}
FLAGS=('no-shader99', 'no-shader99-xlat', 'no-blendcolor', 'no-bordercolor', 'no-tgmem', 'no-vertextex')
MARK=b'made-by-shader99b\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-shader99b' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data={f: open(S+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the y2s99b-shaders one'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('shader99b')
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record shader99b does not match: review'
    for f in (N, L): assert os.path.exists(BAK[f]) and sha(BAK[f])==rec['previous'][f], 'half-installed (backup %s): review'%BAK[f]
    print('SHADER99B_ALREADY'); print('CC_JOB_ADDED shader99b (y2s99b-shaders Nord2Metal + library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record shader99b is there (installed %r): run guest-shader99b-undo.sh first'%rec
    for f in (N, L):
        assert cur[f]!=NEW[f], 'the y2s99b-shaders %s is installed without its record: review'%f
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in OLD[f], 'the installed %s is not the texlayout1 state shader99b is built on: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'a different backup %s is already there'%BAK[f]
        assert not os.path.exists(B+'/'+f+'.nord2tmp') and not os.path.exists(BAK[f]+'.nord2tmp'), 'leftover .nord2tmp'
    assert not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: backups, library, Nord2Metal, receipt ----
    for f in (N, L):
        if not os.path.exists(BAK[f]): put(BAK[f], open(B+'/'+f,'rb').read(), B+'/'+f)
        assert sha(BAK[f])==cur[f]
    for f in (L, N):
        put(B+'/'+f, data[f], B+'/'+f); assert sha(B+'/'+f)==NEW[f]; inv[f]=NEW[f]
    rc['inventory'][REL]=inv
    rc['shader99b']={'previous': dict(cur), 'previous_name': {f: OLD[f][cur[f]] for f in (N, L)}, 'installed': dict(NEW)}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('SHADER99B_INSTALLED', {f: cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'backups', [os.path.relpath(BAK[f],R) for f in (N, L)])
    print('CC_JOB_ADDED shader99b: Nord2Metal + libredwoodair.dylib y2s99b-shaders (translator gaps, blend colour, border colour, threadgroup length)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in (N, L)}, 'record shader99b')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('SHADER99B_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('SHADER99B_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('SHADER99B_NOTE', fl, 'is present and not ours: that feature stays OFF until it is removed')
print('SHADER99B_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
