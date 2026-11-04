set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# usecount1 = y2s84-usecount Nord2Metal (thumbfix5 + a texture over an IOSurface holds the surface's IOSurface use count while it exists;
# the MobileSlideShow freeze rule is dropped once kernel 0398's real use count is seen) over thumbfix5 or an earlier accepted one
# (kept and restored by the undo).  Needs a kernel with 0398 to have an effect; on an older kernel it behaves as thumbfix5.
N='Nord2Metal'; NEW='a6997fb7630c0b20beef7f7044e43575bf95ecdb87cdfcb16050fb5028518f2d'
OLDS={'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f': 'y2s83-thumbfix5', '15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2 (thumbfix3)', 'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'y2s81-thumbcomp5', '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp'}
LIBS={'6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea': 'y2s47', '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': 'libmerge1', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': 'thumbrc', '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': 'libmerge2', '3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be': 'libmerge3', '3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0': 'libmerge4'}
LIB_PREFIXES={'1eae024e': 'libmerge5 (sha256 prefix)', 'fe59bddb': 'libmerge6 (sha256 prefix)'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-usecount1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
if lib not in LIBS: LIBS[lib]=LIB_PREFIXES[lib[:8]]
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read(); assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s84-usecount file'
if cur==NEW:
    assert inv[N]==NEW and (not os.path.exists(BAK) or sha(BAK) in OLDS), 'half-installed: review'
    print('USECOUNT1_ALREADY'); print('CC_JOB_ADDED usecount1 (y2s84-usecount Nord2Metal) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in OLDS and inv[N]==cur, 'the installed Nord2Metal is none of thumbfix5/thumbfix4/thumbfix2/thumbcomp5/4/3/2/thumbcomp: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup is already there'
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('USECOUNT1_INSTALLED', N, OLDS[cur], cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R), 'library', LIBS[lib])
    print('CC_JOB_ADDED usecount1: Nord2Metal = y2s84-usecount (textures hold the IOSurface use count)')
    inv[N]=NEW; rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]})
# usecount1: optional comparison flags shipped in the payload dir (/src/usecount-keepfreeze, /src/usecount-off) are created in
# private/var/db/nord2-metal with a marker; the undo removes only marked ones.
MARK=b'made-by-usecount1\n'
for fl in ('usecount-keepfreeze', 'usecount-off'):
    src='/src/'+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
            os.chmod(dst,0o644)
        print('USECOUNT1_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('USECOUNT1_FLAG', fl, 'cleared (not in this payload)')
print('USECOUNT1_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
