set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, shutil
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gfxall11: Nord2Metal and libredwoodair.dylib put back exactly as record 'gfxall11' says, from the backups *.pre-gfxall11 or -- only for a
# missing backup -- from the copies in this payload dir (shader99b's pair, the mirror's state) when they are exactly those files.
# Backups and record removed; switches marked 'made-by-gfxall' removed, foreign ones kept. Other records untouched.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: '9e0a80cb33616517eb3e9ab459ed8e50dea7cf9d86b1841a82fdad35ecc25fce', L: 'ec36ec3eccffd88c47daf8f0c6969b92fc1a4e2a2bb1b73f04860a71f3e9793d'}
OLD={N: {
              "e6f75db15fd14bcb8e19bd0f42fe8499d16d86685d9ea7e4a333a003a0f1e593": "gfx100a (on webxr1)",
              "36f755ea01782f8424863c74c6c637580d6fffc23c9a85fee99e7d419190ec6d": "webxr1",
              "fe4ecc9f222b45bd125af2df052cf73f34288f11e97b8b0d9043d1d7a552340d": "shader99b",
              "abf0e80b8ff2e441cc5ddf07a69aa288418b436e4663903a719a81d1098df7c3": "gfxall2",
              "744e3fa95dff2e08030f63a22bc9ca252b0ac7b73cfb8ab30d5792a912d55235": "gfxall3",
              "2cb9a64b56d714cca5e3e315044ec9e193b500967daac953a1eb69f2ebe2bc63": "gfxall7",
              "12fa2eb1a68a1ec0fd7f47c81cc50775cab442322d53cb916b0246b562297497": "gfxall8",
              "dd57b17464623da768ea4867ec8cb87b19627b3d0764160edcd853745cdb1a58": "gfxall9",
              "4c8b53dcf2a23430e72063b67bc65787cb5902973ff22a4ab5e3b6a2cfa652a5": "gfxall10"
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
              "1cb5fd62647166ba8358960bdf21800d8c6a0d19abff3685982a699a83f276a6": "gfxall4",
              "71a88a19e7bf66d7af45cdb07f1beeb50486d18c3e8ba472942fec2d9f2d8d73": "gfxall5",
              "937911aa6af7fbba994dc0161c98c3f2b3c9d91346540a1ce8c152f1e9858b2e": "gfxall6",
              "ec36ec3eccffd88c47daf8f0c6969b92fc1a4e2a2bb1b73f04860a71f3e9793d": "gfxall7"
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
FLAGS=('no-gpupipe', 'no-bigpool', 'no-webxr', 'no-gfx100a', 'no-rtcopy', 'no-gfx200', 'no-gfx200-msaartt', 'no-gfx200-zsdc', 'no-gfx200-rtpool', 'no-gfx200-pools', 'no-gfxall3-shadercache', 'no-gfxall3-statecache', 'no-gfxall3-uploadbatch', 'no-gfxall3-bytesring', 'no-gfxall4-push', 'no-gfxall4-budget', 'no-gfxall4-asyncall', 'no-gpuv4', 'trace-cc', 'trace-al', 'no-rt1', 'no-singletarget', 'surf-al', 'matsnap-log', 'no-matsnap', 'mt-nodeep', 'matsnap-allow')
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
RP=DB+'/receipt.json'; BAK={N: DB+'/'+N+'.pre-gfxall11', L: DB+'/'+L+'.pre-gfxall11'}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        fb[f]=open(S+f,'rb').read(); assert hashlib.sha256(fb[f]).hexdigest() in OLD[f], 'payload fallback %s is not an accepted previous state'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gfxall11'); did=[]
for fl in FLAGS:
    if os.path.exists(DB+'/'+fl): assert os.path.isfile(DB+'/'+fl) and not os.path.islink(DB+'/'+fl), 'switch %s is not a regular file'%fl
if not (rec and cur==NEW):
    for f in (N, L):
        assert cur[f] in OLD[f], 'the installed %s is neither gfxall11 nor a state it replaces: %s'%(f, cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
        if rec: assert rec['previous'][f]==cur[f], 'a record gfxall11 is there but the bundle is not its previous state: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('stale backup %s removed'%f)
    if rec:
        dropcache(rec, did)
        rc.pop('gfxall11'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gfxall11 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GFXALL11_UNDONE', did or 'nothing to do (gfxall11 not installed)')
else:
    # ---- all checks first ----
    assert rec and rec.get('installed')==NEW, 'the gfxall11 files are installed without a matching record gfxall11: review'
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
    rc['inventory'][REL]=inv; rc.pop('gfxall11'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    dropcache(rec, did)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append('backup %s removed'%f)
    print('GFXALL11_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record gfxall11 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gfxall11 removed')
LOGD=DB+'/log'; LOGM=LOGD+'/.made-by-gfxall'
if os.path.lexists(LOGD): assert os.path.isdir(LOGD) and not os.path.islink(LOGD), 'log path is not a directory'
if os.path.exists(LOGM) and open(LOGM,'rb').read()==MARK:
    for base,dirs,files in os.walk(LOGD):
        for n in dirs+files: assert not os.path.islink(os.path.join(base,n)), 'a link inside the log directory: review'
    k=len(os.listdir(LOGD)); shutil.rmtree(LOGD); print('GFXALL11_LOGDIR removed (%d entries)'%k)
MGP=R+'/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist'
MGK='Aixt/MEN2O2B7f+8m4TxUA'; MGM=DB+'/mg-nodeep.made-by-gfxall'; MGB=DB+'/MobileGestalt.plist.pre-gfxall9'
import plistlib
def mg_write(data, st):
    tmp=MGP+'.gfxall9-tmp'
    with open(tmp,'xb') as fo: fo.write(data); fo.flush(); os.fsync(fo.fileno())
    os.chown(tmp, st.st_uid, st.st_gid); os.chmod(tmp, st.st_mode & 0o7777); os.rename(tmp, MGP)
def mg_drop():
    # put MobileGestalt's cache back: the saved bytes when the file is still exactly what we wrote, else only our key taken out
    if not os.path.exists(MGM): return
    assert open(MGM,'rb').read().startswith(MARK), 'mg-nodeep marker is not ours: review'
    want=open(MGM,'rb').read()[len(MARK):].decode().strip()
    assert os.path.isfile(MGP) and not os.path.islink(MGP), 'MobileGestalt cache is not a regular file: review'
    st=os.stat(MGP); cur=hashlib.sha256(open(MGP,'rb').read()).hexdigest()
    if cur==want and os.path.isfile(MGB):
        mg_write(open(MGB,'rb').read(), st); how='saved bytes restored'
    else:
        d=plistlib.loads(open(MGP,'rb').read())
        if d.get('CacheExtra',{}).get(MGK) is False: d['CacheExtra'].pop(MGK); mg_write(plistlib.dumps(d, fmt=plistlib.FMT_BINARY), st); how='our key removed (the cache changed since)'
        else: how='key no longer ours, file left alone'
    if os.path.exists(MGB): os.unlink(MGB)
    os.unlink(MGM); print('GFXALL11_MG deep-colour answer taken back:', how)
mg_drop()
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GFXALL11_FLAG', fl, 'removed')
print('GFXALL11_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo UNDO_DONE
echo INSTALL_DONE   # (generic_update_offline.py requires it)
