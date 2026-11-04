set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s73-thumbfix over vt23 / thumbstage / thumbsync (whichever is installed; it is kept and restored by the undo).
N='Nord2Metal'; NEW='ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa'
OLDS={'4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-thumbcomp5'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS or lib[:8] in LIB_PREFIXES, 'unknown libredwoodair.dylib '+lib
if lib not in LIBS: LIBS[lib]=LIB_PREFIXES[lib[:8]]
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read(); assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s81-thumbcomp5 file'
if cur==NEW:
    assert inv[N]==NEW and os.path.exists(BAK) and sha(BAK) in OLDS, 'half-installed: review'
    print('THUMBCOMP5_ALREADY'); print('CC_JOB_ADDED y2s81-thumbcomp5 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in OLDS and inv[N]==cur, 'the installed Nord2Metal is none of thumbcomp4/thumbcomp3/thumbfix2: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup is already there'
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('THUMBCOMP5_INSTALLED', N, OLDS[cur], cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R), 'library', LIBS[lib])
    print('CC_JOB_ADDED y2s81-thumbcomp5: Nord2Metal (thumbfix2 + backboardd composite log to a file)')
    inv[N]=NEW; rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]})
keep=DB+'/kept'   # y2s81: backboardd writes its composite log here (bbdiag-<pid>.log): any process writes (as the vt guests made it)
if not os.path.isdir(keep): os.mkdir(keep)
os.chmod(keep,0o1777); os.chown(keep,0,0)
print('THUMBCOMP5_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries')
print('THUMBCOMP5_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
