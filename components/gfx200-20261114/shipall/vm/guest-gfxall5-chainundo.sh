set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gfxall5 (gpu/build/gfx200-20261114/all5-src): ONE integrated userland = shader99b + webxr1 (XR formats 550-555 as 8-bit) + gfx100a
# (rtcopy, astcedge, file log, slot/surface diagnostics; Nord2Metal e6f75db1 unchanged) + bigpool1 (parked GPU buffers > 16 MiB) +
# gpupipe1 (two rotating tiler heaps + ABI v3 access sets for kernel 0632; v2 fallback on older kernels) + gfxall5 (msaartt: multisample-resolve passes drawn into the resolve
# texture at 4 samples, no resolve blit; zsdc: depth/stencil DontCare not written back; rtpool: render-target-only textures reused;
# pools: y2s91 buffer/texture pools in every process; switches no-gfx200, no-gfx200-msaartt/-zsdc/-rtpool/-pools) + gfxall3 (shader file cache in
# /var/db/nord2-metal/cache -- made here root:wheel 1777 unless it exists, removed WITH its files by the undo only if made here --,
# per-draw state cache, buffer->texture upload batching, set*Bytes ring; switches no-gfxall3-shadercache/-statecache/-uploadbatch/-bytesring) + gfxall4 (every constant buffer pushed into
# uniforms, one pool budget + memory-pressure release, async pipeline builds in every process, read-only shader cache
# /usr/lib/nord2-metal-cache seeded from pancache-seed/ when the payload ships it; switches no-gfxall4-push/-budget/-asyncall) + gpupipe2 (ABI v4:
# up to 128 buffers per job so backboardd's jobs are precise too; falls back to v3/v2 when the kernel refuses; switch no-gpuv4;
# kernel side 0660 = q10jperf2/q10jfinal2). BOTH files are written.
# Accepted current states: any of the userland states of tonight's single payloads (shader99b, webxr1, gfx100a, bigpool1/1x, gpupipe1, gfxall1, gfxall2, gfxall3, gfxall4);
# their records/backups are left alone (their undos refuse while this is installed). Backups *.pre-gfxall5, record 'gfxall5'.
# Switches that the code reads (each feature's own, unchanged): no-webxr, no-gfx100a (+ no-rtcopy no-astcedge no-filelog no-slotdiag),
# no-bigpool, no-gpupipe. This payload makes/clears only the ones in FLAGS, marked 'made-by-gfxall' (foreign ones kept).
# Every check runs before the first write; writes are backups -> library -> Nord2Metal -> receipt.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/inst/'
NEW={N: '2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63', L: '71a88a19e7bf66d7af45cdb07f1beeb50486d18c3e8ba472942fec2d9f2d8d73'}
OLD={N: {
              "e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593": "gfx100a (on webxr1)",
              "36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d": "webxr1",
              "fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d": "shader99b",
              "abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3": "gfxall2",
              "744e3fa95dff2e08030f63a22bc9ca252b0ac7b73cfb8ab30d5792a912d55235": "gfxall3",
              "2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63": "gfxall4"
     },
     L: {
              "f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56": "bigpool1x (on webxr1)",
              "9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a": "webxr1",
              "d1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14": "bigpool1 (on shader99b)",
              "7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56": "shader99b",
              "46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8": "gpupipe1 (on bigpool1)",
              "366b0e1a2448fe51f7b79597360dd93682fcd0c87e6279514118a6a6b2745581": "gfxall1",
              "f25ca5e4cbe7925f49abcfefbf45244e6e422e2d6015108ab699ec735f1e756a": "gfxall2",
              "0fb23f5e10d462a469b31d3040947360f4e402c4b6f82d5df34d8b961eef6238": "gfxall3",
              "1cb5fd62647166ba8358960bdf21800d8c6a0d19abff3685982a699a83f276a6": "gfxall4"
     }}
CD=DB+'/cache'   # gfxall3: the shader file cache directory (root:wheel 1777), made here unless it exists
FLAGS=('no-gpupipe', 'no-bigpool', 'no-webxr', 'no-gfx100a', 'no-rtcopy', 'no-gfx200', 'no-gfx200-msaartt', 'no-gfx200-zsdc', 'no-gfx200-rtpool', 'no-gfx200-pools', 'no-gfxall3-shadercache', 'no-gfxall3-statecache', 'no-gfxall3-uploadbatch', 'no-gfxall3-bytesring', 'no-gfxall4-push', 'no-gfxall4-budget', 'no-gfxall4-asyncall', 'no-gpuv4')
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfxall5', L: DB+'/'+L+'.pre-gfxall5'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data={f: open(S+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the gfxall5 one'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfxall5')
RO=R+'/usr/lib/nord2-metal-cache'; SEED=S+'pancache-seed'
seeds=sorted(os.listdir(SEED)) if os.path.isdir(SEED) else []
for nm in seeds:
    assert len(nm)==43 and nm.endswith('.pc') and all(c in '0123456789abcdef' for c in nm[:40]), 'seed file name '+nm
    p=SEED+'/'+nm; assert os.path.isfile(p) and not os.path.islink(p) and 44 < os.path.getsize(p) <= (2<<20), 'seed file '+nm
    assert open(p,'rb').read(4)==b'N2PC', 'seed magic '+nm
if os.path.lexists(RO): assert os.path.isdir(RO) and not os.path.islink(RO), 'read-only cache path is not a directory'
def put_new(path, data):
    tmp=path+'.nord2tmp'
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,0o644); os.chown(tmp,0,0); os.replace(tmp,path)
if os.path.lexists(CD): assert os.path.isdir(CD) and not os.path.islink(CD), 'cache path is not a directory'
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record gfxall5 does not match: review'
    for f in (N, L): assert os.path.exists(BAK[f]) and sha(BAK[f])==rec['previous'][f], 'half-installed (backup %s): review'%BAK[f]
    print('GFXALL5_ALREADY'); print('CC_JOB_ADDED gfxall5 (Nord2Metal + library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record gfxall5 is there (installed %r): run guest-gfxall5-undo.sh first'%rec
    for f in (N, L):
        assert cur[f]!=NEW[f] or cur[f] in OLD[f], 'the gfxall5 %s is installed without its record: review'%f
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in OLD[f], 'the installed %s is not a state gfxall5 accepts: %s'%(f, cur[f])
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
    made=False
    # gfxall4: the read-only seed directory every process can read, and the seed files this payload ships (pancache-seed/)
    romade=False; seeded=[]
    if not os.path.exists(RO):
        os.mkdir(RO); os.chown(RO,0,0); os.chmod(RO,0o755); romade=True
    for nm in seeds:
        dst=RO+'/'+nm
        if os.path.exists(dst): continue
        put_new(dst, open(SEED+'/'+nm,'rb').read()); seeded.append(nm)
    if not os.path.exists(CD):
        os.mkdir(CD); os.chown(CD,0,0); os.chmod(CD,0o1777); made=True
    rc['gfxall5']={'previous': cur, 'previous_name': {f: OLD[f][cur[f]] for f in (N, L)}, 'installed': NEW, 'cachedir_made': made, 'rodir_made': romade, 'seeded': seeded}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('GFXALL5_INSTALLED', {f: cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'from', {f: OLD[f][cur[f]] for f in (N, L)})
    print('CC_JOB_ADDED gfxall5: Nord2Metal + libredwoodair.dylib (integrated userland)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in (N, L)}, 'record gfxall5')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('GFXALL5_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('GFXALL5_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('GFXALL5_NOTE', fl, 'is present and not ours: that part stays OFF until it is removed')
for fl in ('no-thumbgrid', 'no-thumbgrid-bufpool', 'no-thumbgrid-texpool', 'no-webxr', 'no-gfx100a', 'no-bigpool', 'no-rt1'):
    if os.path.exists(DB+'/'+fl): print('GFXALL5_NOTE', fl, 'is present (another feature\'s switch; gfxall5 parts that depend on it follow it)')
print('GFXALL5_ROCACHE', len(os.listdir(RO)) if os.path.isdir(RO) else 'missing', 'files')
print('GFXALL5_CACHEDIR', oct(os.stat(CD).st_mode&0o7777) if os.path.isdir(CD) else 'missing', len(os.listdir(CD)) if os.path.isdir(CD) else 0, 'files')
print('GFXALL5_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
python3 - <<'GUEST'
import hashlib, json, os, shutil
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gfxall5: Nord2Metal and libredwoodair.dylib put back exactly as record 'gfxall5' says, from the backups *.pre-gfxall5 or -- only for a
# missing backup -- from the copies in this payload dir (shader99b's pair, the mirror's state) when they are exactly those files.
# Backups and record removed; switches marked 'made-by-gfxall' removed, foreign ones kept. Other records untouched.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: '2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63', L: '71a88a19e7bf66d7af45cdb07f1beeb50486d18c3e8ba472942fec2d9f2d8d73'}
OLD={N: {
              "e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593": "gfx100a (on webxr1)",
              "36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d": "webxr1",
              "fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d": "shader99b",
              "abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3": "gfxall2",
              "744e3fa95dff2e08030f63a22bc9ca252b0ac7b73cfb8ab30d5792a912d55235": "gfxall3",
              "2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63": "gfxall4"
     },
     L: {
              "f271fcbe3dde817ea5fa719c32614f50a78df229d14672516c9ace7abf296a56": "bigpool1x (on webxr1)",
              "9f263e6ff8255b193a20bebc8a9def367d2b577b4c0127fccbb7a5c07405207a": "webxr1",
              "d1c90b063e93298ce7876519dd11d6750f25da5c0db92d0ff02d79b790228a14": "bigpool1 (on shader99b)",
              "7c0c5abaf75e54da5bb245cf9d7472564edd2c36113ffdd046193a0766138a56": "shader99b",
              "46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8": "gpupipe1 (on bigpool1)",
              "366b0e1a2448fe51f7b79597360dd93682fcd0c87e6279514118a6a6b2745581": "gfxall1",
              "f25ca5e4cbe7925f49abcfefbf45244e6e422e2d6015108ab699ec735f1e756a": "gfxall2",
              "0fb23f5e10d462a469b31d3040947360f4e402c4b6f82d5df34d8b961eef6238": "gfxall3",
              "1cb5fd62647166ba8358960bdf21800d8c6a0d19abff3685982a699a83f276a6": "gfxall4"
     }}
CD=DB+'/cache'
RO=R+'/usr/lib/nord2-metal-cache'
def dropro(rec, did):
    if not rec: return
    for nm in rec.get('seeded', []):
        p=RO+'/'+nm
        if os.path.isfile(p) and not os.path.islink(p): os.unlink(p)
    if rec.get('seeded'): did.append('%d seed files removed'%len(rec.get('seeded', [])))
    if rec.get('rodir_made') and os.path.isdir(RO) and not os.path.islink(RO):
        assert not os.listdir(RO), 'files not ours in the read-only cache directory: review'
        os.rmdir(RO); did.append('read-only cache directory removed')
def dropcache(rec, did):
    dropro(rec, did)
    if rec and rec.get('cachedir_made') and os.path.lexists(CD):
        assert os.path.isdir(CD) and not os.path.islink(CD), 'cache path is not a directory'
        for base,dirs,files in os.walk(CD):
            for n in dirs+files: assert not os.path.islink(os.path.join(base,n)), 'a link inside the cache directory: review'
        k=len(os.listdir(CD)); shutil.rmtree(CD); did.append('cache directory removed (%d entries)'%k)
FLAGS=('no-gpupipe', 'no-bigpool', 'no-webxr', 'no-gfx100a', 'no-rtcopy', 'no-gfx200', 'no-gfx200-msaartt', 'no-gfx200-zsdc', 'no-gfx200-rtpool', 'no-gfx200-pools', 'no-gfxall3-shadercache', 'no-gfxall3-statecache', 'no-gfxall3-uploadbatch', 'no-gfxall3-bytesring', 'no-gfxall4-push', 'no-gfxall4-budget', 'no-gfxall4-asyncall', 'no-gpuv4')
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfxall5', L: DB+'/'+L+'.pre-gfxall5'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        fb[f]=open(S+f,'rb').read(); assert hashlib.sha256(fb[f]).hexdigest() in OLD[f], 'payload fallback %s is not an accepted previous state'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfxall5'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if not (rec and cur==NEW):
    for f in (N, L):
        assert cur[f] in OLD[f], 'the installed %s is neither gfxall5 nor a state it replaces: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
        if rec: assert rec['previous'][f]==cur[f], 'a record gfxall5 is there but the bundle is not its previous state: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('stale backup %s removed'%f)
    if rec:
        dropcache(rec, did)
        rc.pop('gfxall5'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gfxall5 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GFXALL5_UNDONE', did or 'nothing to do (gfxall5 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the gfxall5 files are installed without a matching record gfxall5: review'
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
    rc['inventory'][REL]=inv; rc.pop('gfxall5'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    dropcache(rec, did)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('backup %s removed'%f)
    print('GFXALL5_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record gfxall5 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gfxall5 removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GFXALL5_FLAG', fl, 'removed')
print('GFXALL5_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
