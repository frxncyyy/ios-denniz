set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of photosperf1: the backed-up Nord2Metal (thumbfix5 or thumbfix4) back, and the no-photosperf flag removed when photosperf1-off made it.
N='Nord2Metal'; NEW='b5e8a282738e6c457e3444668f2f3db7ca05df6852f86c91e072e1937b1f4b3f'
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-photosperf1'; FLAG=DB+'/no-photosperf'; MARK=b'made-by-photosperf1\n'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
cur=sha(B+'/'+N)
did=[]
if os.path.isfile(FLAG) and open(FLAG,'rb').read()==MARK:
    os.unlink(FLAG); did.append('no-photosperf flag (made by photosperf1-off) removed')
if cur==NEW and not os.path.exists(BAK):
    print('PHOTOSPERF1_UNDONE', did or 'nothing to do (photosperf1 was installed before)'); print('CC_JOB_ADDED photosperf1 removed'); print('CC_RECEIPT_UPDATED unchanged')
elif cur==NEW:
    assert sha(BAK) in OLDS, 'no usable backup of the earlier Nord2Metal'
    old=open(BAK,'rb').read(); put(B+'/'+N, old, B+'/'+N); did.append('Nord2Metal restored to '+OLDS[sha(BAK)])
    inv[N]=sha(B+'/'+N); rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    os.unlink(BAK); did.append('backup removed')
    print('PHOTOSPERF1_UNDONE', did); print('CC_JOB_ADDED photosperf1 (y2s85-photosperf Nord2Metal) removed'); print('CC_RECEIPT_UPDATED inventory', {N: inv[N][:12]})
else:
    assert cur in OLDS, 'the installed Nord2Metal is neither photosperf1 nor thumbfix5/thumbfix4: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'unexpected backup'; os.unlink(BAK); did.append('stale backup removed')
    print('PHOTOSPERF1_UNDONE', did or 'nothing to do'); print('CC_JOB_ADDED photosperf1 (y2s85-photosperf Nord2Metal) removed'); print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('PHOTOSPERF1_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
