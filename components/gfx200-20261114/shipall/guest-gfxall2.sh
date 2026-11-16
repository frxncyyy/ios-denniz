set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gfxall2 (gpu/build/gfx200-20261114/all2-src): ONE integrated userland = shader99b + webxr1 (XR formats 550-555 as 8-bit) + gfx100a
# (rtcopy, astcedge, file log, slot/surface diagnostics; Nord2Metal e6f75db1 unchanged) + bigpool1 (parked GPU buffers > 16 MiB) +
# gpupipe1 (two rotating tiler heaps + ABI v3 access sets for kernel 0632; v2 fallback on older kernels) + gfxall2 (msaartt: multisample-resolve passes drawn into the resolve
# texture at 4 samples, no resolve blit; zsdc: depth/stencil DontCare not written back; rtpool: render-target-only textures reused;
# pools: y2s91 buffer/texture pools in every process; switches no-gfx200, no-gfx200-msaartt/-zsdc/-rtpool/-pools). BOTH files are written.
# Accepted current states: any of the userland states of tonight's single payloads (shader99b, webxr1, gfx100a, bigpool1/1x, gpupipe1, gfxall1);
# their records/backups are left alone (their undos refuse while this is installed). Backups *.pre-gfxall2, record 'gfxall2'.
# Switches that the code reads (each feature's own, unchanged): no-webxr, no-gfx100a (+ no-rtcopy no-astcedge no-filelog no-slotdiag),
# no-bigpool, no-gpupipe. This payload makes/clears only the ones in FLAGS, marked 'made-by-gfxall' (foreign ones kept).
# Every check runs before the first write; writes are backups -> library -> Nord2Metal -> receipt.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3', L: 'f25ca5e4cbe7925f49abcfefbf45244e6e422e2d6015108ab699ec735f1e756a'}
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
              "46e556ef49a1d873c768c73e424ae239e2b7be86a0d4b9a5df212713f0d860b8": "gpupipe1 (on bigpool1)",
              "366b0e1a2448fe51f7b79597360dd93682fcd0c87e6279514118a6a6b2745581": "gfxall1"
     }}
FLAGS=('no-gpupipe', 'no-bigpool', 'no-webxr', 'no-gfx100a', 'no-rtcopy', 'no-gfx200', 'no-gfx200-msaartt', 'no-gfx200-zsdc', 'no-gfx200-rtpool', 'no-gfx200-pools')
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfxall2', L: DB+'/'+L+'.pre-gfxall2'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data={f: open(S+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the gfxall2 one'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfxall2')
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record gfxall2 does not match: review'
    for f in (N, L): assert os.path.exists(BAK[f]) and sha(BAK[f])==rec['previous'][f], 'half-installed (backup %s): review'%BAK[f]
    print('GFXALL2_ALREADY'); print('CC_JOB_ADDED gfxall2 (Nord2Metal + library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record gfxall2 is there (installed %r): run guest-gfxall2-undo.sh first'%rec
    for f in (N, L):
        assert cur[f]!=NEW[f] or cur[f] in OLD[f], 'the gfxall2 %s is installed without its record: review'%f
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in OLD[f], 'the installed %s is not a state gfxall2 accepts: %s'%(f, cur[f])
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
    rc['gfxall2']={'previous': cur, 'previous_name': {f: OLD[f][cur[f]] for f in (N, L)}, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('GFXALL2_INSTALLED', {f: cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'from', {f: OLD[f][cur[f]] for f in (N, L)})
    print('CC_JOB_ADDED gfxall2: Nord2Metal + libredwoodair.dylib (integrated userland)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in (N, L)}, 'record gfxall2')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as fo: fo.write(MARK); fo.flush(); os.fsync(fo.fileno())
            os.chmod(dst,0o644)
        print('GFXALL2_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('GFXALL2_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('GFXALL2_NOTE', fl, 'is present and not ours: that part stays OFF until it is removed')
for fl in ('no-thumbgrid', 'no-thumbgrid-bufpool', 'no-thumbgrid-texpool', 'no-webxr', 'no-gfx100a', 'no-bigpool', 'no-rt1'):
    if os.path.exists(DB+'/'+fl): print('GFXALL2_NOTE', fl, 'is present (another feature\'s switch; gfxall2 parts that depend on it follow it)')
print('GFXALL2_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
