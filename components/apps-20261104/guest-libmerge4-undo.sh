set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# Undo of y2s75-libmerge4: libredwoodair.dylib back to the library it replaced (receipt 'libmerge4'.previous: normally y2s72-libmerge3 3c71650a,
# or the y2s47 base 6df1af45, or an earlier variant), from the backup libredwoodair.dylib.pre-libmerge4. The earlier variants' own backups and
# records (libmerge3's .pre-libmerge3 / 'libmerge3') were left alone and stay. /src/libredwoodair.dylib is libmerge3 3c71650a and
# /src/libredwoodair.dylib.base is the base 6df1af45: used only when the backup is gone and the record says one of them.
N='libredwoodair.dylib'
NEW='3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
LM3='3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be'
FROM={BASE: 'y2s47 base', LM3: 'y2s72-libmerge3', '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': 'y2s70-libmerge2',
      '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': 'y2s67-libmerge1', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': 'y2s66-thumbrc',
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': 'y2s65-cam-stats', 'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': 'y2s63-gnr1'}
NMS={'92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f': 'thumbsync', '59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d': 'vt23',
     '6482590ef28a719de78dfa2eeb91bd33524273f6c6b9218b79d7f3e1fc283b2e': 'thumbstage', '15f8c23ecfe39ee03b95ada9131e235ba846ddc00cde93f0fbd0555773520573': 'thumbfix',
     'd86aede99ec8042a9bd9baa44dce2870a05f9c128ef3446f6dbe650a52c88b55': 'gpuview', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'thumbfix2',
     '8d5d3c67841be0f9a3b09b1e6ff6e84a80e01251e0147593c5f5831fdbca33dc': 'thumbdiag', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'thumbcomp',
     '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'thumbcomp2'}
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-libmerge4'
sha=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest()
def inventory(d):       # as tools/stage_metal_root.py inventory(): every entry of the bundle, files by hash
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge4'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
nm=sha(B+'/Nord2Metal')
assert nm in NMS, 'the installed Nord2Metal is none of %s: %s'%(', '.join(NMS.values()), nm)
SRCP=['/src/'+f for f in (N, N+'.base')]
SRC={hashlib.sha256(open(p,'rb').read()).hexdigest(): p for p in SRCP}
assert set(SRC)=={LM3, BASE}, 'undo payload is not libmerge3 + base'
cur=sha(B+'/'+N); m=rc.get('libmerge4')
did=[]
if cur==NEW:
    assert m and m.get('previous') in FROM and m.get('installed')==NEW, 'libmerge4 is installed but the receipt has no usable libmerge4 record: review'
    prev=m['previous']
    if os.path.exists(BAK):
        assert sha(BAK)==prev, 'backup %s is not the recorded previous library: %s'%(os.path.relpath(BAK,R), sha(BAK))
        data=open(BAK,'rb').read()
    else:
        assert prev in SRC, 'no backup of the previous library (%s) and it is neither libmerge3 nor the base: review'%FROM[prev]
        data=open(SRC[prev],'rb').read(); did.append('backup missing: %s taken from the undo payload'%FROM[prev])
    put(B+'/'+N, data, B+'/'+N); assert sha(B+'/'+N)==prev
    did.append('%s restored to %s %s'%(N, FROM[prev], prev[:12]))
else:
    assert cur in FROM and not m and not (os.path.exists(BAK) and sha(BAK)!=cur), '%s is not libmerge4 and not a clean earlier state: %s'%(N,cur)
    prev=cur
if os.path.exists(BAK): os.unlink(BAK); did.append('backup removed')
if os.path.exists(MK):
    if os.path.exists(SW): os.unlink(SW); did.append('gnr1 trace switch removed')
    os.unlink(MK); did.append('marker removed')
print('LIBMERGE4_UNDONE', did or 'nothing to do', 'Nord2Metal', NMS[nm], '(not changed)')
print('CC_JOB_ADDED y2s75-libmerge4 removed')
want=dict(inv); want[N]=prev
if want!=inv or 'libmerge4' in rc:
    rc['inventory'][REL]=want; rc.pop('libmerge4', None); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory', {N: prev[:12]}, 'libmerge4 record removed')
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the undo'
print('LIBMERGE4_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
