set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# thumbfix5 = y2s83-thumbfix5 Nord2Metal (thumbfix4 + MobileSlideShow freezes a converted thumbnail only once its picture is finished) over
# thumbfix4/thumbfix3/thumbfix2 or a thumbcomp one (kept and restored by the undo); kernel q10jfe (0392) as on the phone.
N='Nord2Metal'; NEW='f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f'
OLDS={'15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2 (thumbfix3)', 'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'y2s81-thumbcomp5', '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-thumbfix5'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
if lib not in LIBS: LIBS[lib]=LIB_PREFIXES[lib[:8]]
cur=sha(B+'/'+N)
did=[]
if cur==NEW and not os.path.exists(BAK):
    print('THUMBFIX5_UNDONE', 'nothing to do (thumbfix5 was installed before)'); print('CC_JOB_ADDED thumbfix5 removed'); print('CC_RECEIPT_UPDATED unchanged')
elif cur==NEW:
    assert sha(BAK) in OLDS, 'no usable backup of the earlier Nord2Metal'
    old=open(BAK,'rb').read(); put(B+'/'+N, old, B+'/'+N); did.append('Nord2Metal restored to '+OLDS[sha(BAK)])
    inv[N]=sha(B+'/'+N); rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    os.unlink(BAK); did.append('backup removed')
    print('THUMBFIX5_UNDONE', did); print('CC_JOB_ADDED thumbfix5 (y2s83-thumbfix5 Nord2Metal) removed'); print('CC_RECEIPT_UPDATED inventory', {N: inv[N][:12]})
else:
    assert cur in OLDS, 'the installed Nord2Metal is neither thumbfix5 nor thumbfix2/a known thumbcomp: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'unexpected backup'; os.unlink(BAK); did.append('stale backup removed')
    print('THUMBFIX5_UNDONE', did or 'nothing to do'); print('CC_JOB_ADDED thumbfix5 (y2s83-thumbfix5 Nord2Metal) removed'); print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('THUMBFIX5_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
