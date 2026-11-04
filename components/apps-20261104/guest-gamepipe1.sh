set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gamepipe1 = y2s86-gamepipe Nord2Metal (thumbfix5 + 'GP slow commit anatomy' lines in every process + for third-party apps only:
# content-keyed render pipeline reuse ('GP pso') and really asynchronous completionHandler pipeline builds; switch
# /var/db/nord2-metal/no-gamepipe = thumbfix5 behaviour) over thumbfix5 or thumbfix4 (kept and restored by the undo).
# Userland only: any kernel. NOT over photosperf1/usecount builds (sibling Nord2Metal files: integrate first).
N='Nord2Metal'; NEW='4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7'
OLDS={'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f': 'y2s83-thumbfix5', '15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-gamepipe1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
if lib not in LIBS: LIBS[lib]=LIB_PREFIXES[lib[:8]]
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read(); assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s86-gamepipe file'
if cur==NEW:
    assert inv[N]==NEW and (not os.path.exists(BAK) or sha(BAK) in OLDS), 'half-installed: review'
    print('GAMEPIPE1_ALREADY'); print('CC_JOB_ADDED gamepipe1 (y2s86-gamepipe Nord2Metal) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in OLDS and inv[N]==cur, 'the installed Nord2Metal is neither thumbfix5 nor thumbfix4: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup is already there'
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('GAMEPIPE1_INSTALLED', N, OLDS[cur], cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R), 'library', LIBS[lib])
    print('CC_JOB_ADDED gamepipe1: Nord2Metal = y2s86-gamepipe (GP slow commit anatomy; app pipeline reuse + async builds)')
    inv[N]=NEW; rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]})
for f in ('no-gamepipe',):
    print('GAMEPIPE1_SWITCH', f, 'present' if os.path.exists(DB+'/'+f) else 'absent')
print('GAMEPIPE1_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
