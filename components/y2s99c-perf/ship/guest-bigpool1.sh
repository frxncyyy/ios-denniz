set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# bigpool1 = y2s99c-perf: shader99b's libredwoodair.dylib (7c0c5aba, THE PHONE) + big-buffer reuse in the DRM layer (nord2_pan.c):
# closed GPU buffers over 16 MiB (Maps' full-screen MSAA/depth/intermediate textures, 12..44 MiB, re-created ~50 times a second)
# are kept in 4 slots (<= 100 MiB) and handed to the next request of the same size or up to 1/8 smaller once the GPU is done.
# ONLY libredwoodair.dylib changes; Nord2Metal must be shader99b's fe4ecc9f and is not touched.
# Accepted current state: exactly shader99b (Nord2Metal fe4ecc9f + library 7c0c5aba); its record/backups are left for its own undo.
# Backup libredwoodair.dylib.pre-bigpool1, record 'bigpool1' (guest-bigpool1-undo.sh).
# Switch /var/db/nord2-metal/no-bigpool (old behaviour, read at each process's first GPU open): made here only when the payload dir
# ships it (payload-bigpool1-off), marker 'made-by-bigpool1'; a marked one not in the payload is cleared; a foreign one is kept.
# Every check runs before the first write; writes are backup -> library -> receipt.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={L: 'd1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14'}
OLD={L: {'7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56': 'shader99b library (y2s99b-shaders)'}}
KEEP={N: 'fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d'}
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
data={L: open(S+L,'rb').read()}
assert hashlib.sha256(data[L]).hexdigest()==NEW[L], 'payload file %s is not the y2s99c-perf one'%L
assert sha(B+'/'+N)==KEEP[N], 'the installed Nord2Metal is not shader99b fe4ecc9f (the only state bigpool1 is built on)'
cur={L: sha(B+'/'+L)}; rec=rc.get('bigpool1')
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record bigpool1 does not match: review'
    assert os.path.exists(BAK[L]) and sha(BAK[L])==rec['previous'][L], 'half-installed (backup %s): review'%BAK[L]
    print('BIGPOOL1_ALREADY'); print('CC_JOB_ADDED bigpool1 (y2s99c-perf library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record bigpool1 is there (installed %r): run guest-bigpool1-undo.sh first'%rec
    assert cur[L]!=NEW[L], 'the y2s99c-perf library is installed without its record: review'
    assert inv[L]==cur[L], 'receipt/bundle mismatch for '+L
    assert cur[L] in OLD[L], 'the installed library is not shader99b\'s 7c0c5aba (the only state bigpool1 is built on): %s'%cur[L]
    if os.path.exists(BAK[L]): assert sha(BAK[L])==cur[L], 'a different backup %s is already there'%BAK[L]
    assert not os.path.exists(B+'/'+L+'.nord2tmp') and not os.path.exists(BAK[L]+'.nord2tmp'), 'leftover .nord2tmp'
    assert not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: backup, library, receipt ----
    if not os.path.exists(BAK[L]): put(BAK[L], open(B+'/'+L,'rb').read(), B+'/'+L)
    assert sha(BAK[L])==cur[L]
    put(B+'/'+L, data[L], B+'/'+L); assert sha(B+'/'+L)==NEW[L]; inv[L]=NEW[L]
    rc['inventory'][REL]=inv
    rc['bigpool1']={'previous': dict(cur), 'previous_name': {L: OLD[L][cur[L]]}, 'installed': dict(NEW)}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('BIGPOOL1_INSTALLED', {L: cur[L][:12]+' -> '+NEW[L][:12]}, 'backup', os.path.relpath(BAK[L],R))
    print('CC_JOB_ADDED bigpool1: libredwoodair.dylib y2s99c-perf (reuse of closed GPU buffers over 16 MiB)')
    print('CC_RECEIPT_UPDATED inventory', {L: NEW[L][:12]}, 'record bigpool1')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('BIGPOOL1_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('BIGPOOL1_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('BIGPOOL1_NOTE', fl, 'is present and not ours: big-buffer reuse stays OFF until it is removed')
print('BIGPOOL1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
