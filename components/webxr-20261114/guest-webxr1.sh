set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# webxr1 = shader99b (y2s99b-shaders + api99a) + the extended-range formats 550-555 served as 8-bit BGRA/BGRX over the kernel's 'BGRA'
# surfaces (library table + Nord2Metal import): WebKit's GPU process draws Safari's page tiles ('w30r', Metal 554) -- refused before,
# pages black. BOTH files change. Accepted current state: shader99b only (Nord2Metal fe4ecc9f + library 7c0c5aba); its record/backups are
# left for its own undo. Backups *.pre-webxr1, record 'webxr1' (guest-webxr1-undo.sh). Switch /var/db/nord2-metal/no-webxr (old refusal)
# made only when the payload dir ships it (marker 'made-by-webxr1'); a marked one not in the payload is cleared; a foreign one is kept.
# Every check runs before the first write; writes are backups -> library -> Nord2Metal -> receipt.
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
data={f: open(S+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the webxr1 build'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('webxr1')
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record webxr1 does not match: review'
    for f in (N, L): assert os.path.exists(BAK[f]) and sha(BAK[f])==rec['previous'][f], 'half-installed (backup %s): review'%BAK[f]
    print('WEBXR1_ALREADY'); print('CC_JOB_ADDED webxr1 (Nord2Metal + library, XR formats as 8-bit) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record webxr1 is there (installed %r): run guest-webxr1-undo.sh first'%rec
    for f in (N, L):
        assert cur[f]!=NEW[f], 'the webxr1 %s is installed without its record: review'%f
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in OLD[f], 'the installed %s is not the shader99b state webxr1 is built on: %s'%(f, cur[f])
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
    rc['webxr1']={'previous': dict(cur), 'previous_name': {f: OLD[f][cur[f]] for f in (N, L)}, 'installed': dict(NEW)}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('WEBXR1_INSTALLED', {f: cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'backups', [os.path.relpath(BAK[f],R) for f in (N, L)])
    print('CC_JOB_ADDED webxr1: Nord2Metal + libredwoodair.dylib = shader99b + Metal 550-555 as 8-bit BGRA/BGRX (Safari page tiles)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in (N, L)}, 'record webxr1')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('WEBXR1_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('WEBXR1_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('WEBXR1_NOTE', fl, 'is present and not ours: that feature stays OFF until it is removed')
print('WEBXR1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
