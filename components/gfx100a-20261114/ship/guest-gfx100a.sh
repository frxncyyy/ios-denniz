set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gfx100a = webxr1's Nord2Metal (36f755ea, gpu/build/webxr-20261114: shader99b + XR formats 550-555 as 8-bit) + the gfx100a block
# (gpu/build/gfx100a-20261114/src2): rtcopy (what the GPU renders into a one-plane IOSurface served as a copy -- rows not a multiple of
# 64 bytes, e.g. the switcher snapshots 788x1706 RGBA16F at 6304 bytes a row -- is written back into the surface), astcedge (the last
# partial block row/column of an ASTC upload), and diagnostics: file log /var/db/nord2-metal/log/<process>.<pid>.log, the pipelines that
# draw with an empty texture slot, the refused IOSurface textures. ONLY Nord2Metal changes; the library must be webxr1's 9f263e6f.
# Accepted current state: Nord2Metal 36f755ea + library 9f263e6f with record 'webxr1' (left for its own undo).
# Backup Nord2Metal.pre-gfx100a, record 'gfx100a' (guest-gfx100a-undo.sh). The log directory private/var/db/nord2-metal/log is made
# here (root:wheel 1777) unless it exists; the record says whether we made it, and only then does the undo remove it.
# Switches (/var/db/nord2-metal/): no-gfx100a (all off, file log included), no-rtcopy, no-astcedge, no-filelog, no-slotdiag -- made
# here only when the payload dir ships them (marker 'made-by-gfx100a'); a marked one not in the payload is cleared; a foreign one kept.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW='e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593'
OLD={'36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d': 'webxr1 (webxr-20261114 on shader99b)'}
LIB='9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-gfx100a'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data=open(S+N,'rb').read()
assert hashlib.sha256(data).hexdigest()==NEW, 'payload file Nord2Metal is not the gfx100a one'
cur=sha(B+'/'+N); rec=rc.get('gfx100a')
assert sha(B+'/'+L)==LIB and inv[L]==LIB, 'the installed library is not webxr1\'s 9f263e6f: gfx100a is built for it'
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if os.path.lexists(LOGD): assert os.path.isdir(LOGD) and not os.path.islink(LOGD), 'log path is not a directory'
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record gfx100a does not match: review'
    assert os.path.exists(BAK) and sha(BAK)==rec['previous'], 'half-installed (backup %s): review'%BAK
    print('GFX100A_ALREADY'); print('CC_JOB_ADDED gfx100a (Nord2Metal) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record gfx100a is there (installed %r): run guest-gfx100a-undo.sh first'%rec
    assert cur!=NEW, 'the gfx100a Nord2Metal is installed without its record: review'
    assert inv[N]==cur, 'receipt/bundle mismatch for Nord2Metal'
    assert cur in OLD, 'the installed Nord2Metal is not the webxr1 state gfx100a is built on: %s'%cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there'%BAK
    assert not os.path.exists(B+'/'+N+'.nord2tmp') and not os.path.exists(BAK+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: backup, log directory, Nord2Metal, receipt ----
    if not os.path.exists(BAK): put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    made=False
    if not os.path.exists(LOGD):
        os.mkdir(LOGD); os.chown(LOGD,0,0); os.chmod(LOGD,0o1777); made=True
    put(B+'/'+N, data, B+'/'+N); assert sha(B+'/'+N)==NEW; inv[N]=NEW
    rc['inventory'][REL]=inv
    rc['gfx100a']={'previous': cur, 'previous_name': OLD[cur], 'installed': NEW, 'library': LIB, 'logdir_made': made}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('GFX100A_INSTALLED', cur[:12]+' -> '+NEW[:12], 'backup', os.path.relpath(BAK,R), 'logdir', 'made' if made else 'existing')
    print('CC_JOB_ADDED gfx100a: Nord2Metal (rtcopy write-back, ASTC edge, file log + slot/surface diagnostics)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'record gfx100a')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('GFX100A_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('GFX100A_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('GFX100A_NOTE', fl, 'is present and not ours: that feature stays OFF until it is removed')
print('GFX100A_LOGDIR', oct(os.stat(LOGD).st_mode&0o7777), len(os.listdir(LOGD)), 'files')
print('GFX100A_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
