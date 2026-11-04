set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# api99a = y2s99a-api-gaps: texlayout1 (y2s98, THE PHONE) + the API-surface gaps: MTLPipelineLibrary with an empty per-GPU
# cache (ActivityRingsUI, NanoTimeKit, SiriUICore, LocalAuthenticationPrivateUI, PassKitUIFoundation, PencilKit, TextInputUI;
# QuartzCore/RenderBox/VectorKit left alone), IOSurface sub-rectangle textures, -newFence, encodeSignal/WaitForEvent, numFaces,
# the family-query log and the empty-slot diagnosis. ONLY Nord2Metal changes; the library stays gpufinal6's d9dd236e.
# Accepted current state: exactly texlayout1 (Nord2Metal 3ae0c2f8 + libredwoodair.dylib d9dd236e); its record/backup are left for
# its own undo. Backup Nord2Metal.pre-api99a, record 'api99a' (restored/removed by guest-api99a-undo.sh).
# Switch /var/db/nord2-metal/no-api99a = all y2s99a features off (texlayout1 behaviour): made here only if the payload dir ships it
# (payload-api99a-off), marker 'made-by-api99a'; a marked one not in the payload is cleared; one that is not ours is kept and
# reported. Per-feature switches (no-plib, no-subrect, no-fence, no-events, no-faces, no-famlog, no-emptyslot) are by hand only.
# Every check runs before the first write; writes are backup -> Nord2Metal -> receipt.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW='d86b3f9f9b731e35fc594d78e11de5efb9ba5793b24d19f3400e1357be163363'
OLD={'3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d': 'texlayout1 (y2s98-texlayout)'}
LIB='d9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb'
FLAGS=('no-api99a',)
MARK=b'made-by-api99a\n'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-api99a'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data=open(S+N,'rb').read()
assert hashlib.sha256(data).hexdigest()==NEW, 'payload file Nord2Metal is not the y2s99a-api-gaps one'
cur=sha(B+'/'+N); lib=sha(B+'/'+L); rec=rc.get('api99a')
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur==NEW and rec:
    assert rec.get('installed')=={N: NEW} and lib==LIB, 'record api99a does not match (or the library changed): review'
    assert os.path.exists(BAK) and sha(BAK)==rec['previous'][N], 'half-installed (backup %s missing or different): review'%BAK
    print('API99A_ALREADY'); print('CC_JOB_ADDED api99a (y2s99a-api-gaps Nord2Metal) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first ----
    assert not rec, 'a stale receipt record api99a is there (installed %r): run guest-api99a-undo.sh first'%rec
    assert cur!=NEW, 'the y2s99a-api-gaps Nord2Metal is installed without its record: review'
    assert inv[N]==cur and inv[L]==lib, 'receipt/bundle mismatch'
    assert cur in OLD, 'the installed Nord2Metal is not texlayout1 (the only state api99a is built on): %s'%cur
    assert lib==LIB, 'the installed libredwoodair.dylib is not gpufinal6\'s d9dd236e (api99a leaves the library as it is): %s'%lib
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there'%BAK
    assert not os.path.exists(B+'/'+N+'.nord2tmp') and not os.path.exists(BAK+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: backup, Nord2Metal, receipt ----
    if not os.path.exists(BAK): put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    put(B+'/'+N, data, B+'/'+N); assert sha(B+'/'+N)==NEW; inv[N]=NEW
    rc['inventory'][REL]=inv
    rc['api99a']={'previous': {N: cur}, 'previous_name': {N: OLD[cur]}, 'installed': {N: NEW}, 'library': lib}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('API99A_INSTALLED', OLD[cur]+' '+cur[:12]+' -> '+NEW[:12], 'backup', os.path.relpath(BAK,R), 'library left', lib[:12])
    print('CC_JOB_ADDED api99a: Nord2Metal y2s99a-api-gaps (texlayout1 + pipeline libraries, IOSurface sub-rectangles, fences, events, numFaces, family log; switch no-api99a)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'record api99a')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
            os.chmod(dst,0o644)
        print('API99A_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('API99A_FLAG', fl, 'cleared (not in this payload)')
    elif os.path.exists(dst):
        print('API99A_NOTE', fl, 'is present and not ours: the y2s99a features stay OFF (texlayout1 behaviour) until it is removed')
print('API99A_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
