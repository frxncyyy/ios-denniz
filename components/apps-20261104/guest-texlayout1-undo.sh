set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of texlayout1 (y2s98-texlayout Nord2Metal 3ae0c2f8): Nord2Metal is put back exactly as the receipt record 'texlayout1' says
# (previous = gpufinal6 fe406e9c), from its backup Nord2Metal.pre-texlayout1, or -- only if that backup is missing -- from the copy
# shipped in this payload dir when it is exactly that previous file. The library is not touched (texlayout1 never changed it).
# Backup and record removed; the switch no-texlayout removed if it carries the marker 'made-by-texlayout1', kept otherwise.
# gpufinal6's own record/backups are untouched, so guest-gpufinal6-undo.sh works afterwards.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW='3ae0c2f8fcd43a4b59ef0cb014df329f0231d960bbbf732dd03960e576227f3d'
OLD={'fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe': 'gpufinal6 (y2s96-final)'}
FLAGS=('no-texlayout',)
MARK=b'made-by-texlayout1\n'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-texlayout1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb=None
if os.path.exists(S+N):
    fb=open(S+N,'rb').read(); assert hashlib.sha256(fb).hexdigest() in OLD, 'payload fallback Nord2Metal is not gpufinal6'
cur=sha(B+'/'+N); rec=rc.get('texlayout1'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if cur!=NEW:
    # texlayout1 is not installed: only stale leftovers (a backup equal to the current file, a record naming it) may be cleaned
    assert cur in OLD, 'the installed Nord2Metal is neither texlayout1 nor gpufinal6: %s'%cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'unexpected backup '+BAK
    if rec: assert rec.get('previous')=={N: cur}, 'a record texlayout1 is there but Nord2Metal is neither texlayout1 nor its previous file: review'
    if os.path.exists(BAK): os.unlink(BAK); did.append('stale backup removed')
    if rec:
        rc.pop('texlayout1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record texlayout1 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('TEXLAYOUT1_UNDONE', did or 'nothing to do (texlayout1 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')=={N: NEW}, 'the y2s98-texlayout Nord2Metal is installed without a matching record texlayout1: review'
    prev=rec['previous'][N]; assert prev in OLD, 'the record names an unknown previous Nord2Metal: %s'%prev
    assert inv[N]==cur, 'receipt/bundle mismatch'
    if os.path.exists(BAK) and sha(BAK)==prev: src=(open(BAK,'rb').read(), 'backup')
    elif not os.path.exists(BAK) and fb is not None and hashlib.sha256(fb).hexdigest()==prev: src=(fb, 'fallback in the undo payload')
    else: raise AssertionError('no usable copy of the previous Nord2Metal (%s): backup %s'%(prev, 'present but different' if os.path.exists(BAK) else 'missing'))
    assert not os.path.exists(B+'/'+N+'.nord2tmp') and not os.path.exists(RP+'.nord2tmp'), 'leftover .nord2tmp'
    # ---- writes: Nord2Metal, receipt, backup ----
    put(B+'/'+N, src[0], B+'/'+N); inv[N]=sha(B+'/'+N); assert inv[N]==prev
    did.append('Nord2Metal restored to %s %s (%s)'%(OLD[prev], prev[:12], src[1]))
    rc['inventory'][REL]=inv; rc.pop('texlayout1'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    if os.path.exists(BAK): os.unlink(BAK); did.append('backup removed')
    print('TEXLAYOUT1_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {N: prev[:12]}, 'record texlayout1 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED texlayout1 (y2s98-texlayout Nord2Metal) removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('TEXLAYOUT1_FLAG', fl, 'removed')
    elif os.path.exists(dst): print('TEXLAYOUT1_NOTE', fl, 'is present and not ours: kept')
print('TEXLAYOUT1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
