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
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/webxr1/'
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
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/gfxinst/'
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
