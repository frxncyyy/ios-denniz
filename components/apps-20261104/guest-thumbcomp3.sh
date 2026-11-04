set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s73-thumbfix over vt23 / thumbstage / thumbsync (whichever is installed; it is kept and restored by the undo).
N='Nord2Metal'; NEW='2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230'
OLDS={'36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2'}
LIBS={'6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea': 'y2s47', '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': 'libmerge1', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': 'thumbrc', '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': 'libmerge2', '3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be': 'libmerge3', '3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0': 'libmerge4'}
LIB_PREFIXES={'1eae024e': 'libmerge5 (sha256 prefix: the full hash was not on the host when this was written)'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-thumbcomp3'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
if lib not in LIBS: LIBS[lib]=LIB_PREFIXES[lib[:8]]
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read(); assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s79-thumbcomp3 file'
if cur==NEW:
    assert inv[N]==NEW and os.path.exists(BAK) and sha(BAK) in OLDS, 'half-installed: review'
    print('THUMBCOMP3_ALREADY'); print('CC_JOB_ADDED y2s79-thumbcomp3 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in OLDS and inv[N]==cur, 'the installed Nord2Metal is none of thumbcomp2/thumbcomp/thumbfix2: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup is already there'
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('THUMBCOMP3_INSTALLED', N, OLDS[cur], cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R), 'library', LIBS[lib])
    print('CC_JOB_ADDED y2s79-thumbcomp3: Nord2Metal (thumbfix2 + backboardd draw-by-draw composite log)')
    inv[N]=NEW; rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]})
print('THUMBCOMP3_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
