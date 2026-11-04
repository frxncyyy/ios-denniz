set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s75-libmerge4: libredwoodair.dylib only = y2s72-libmerge3 + pooled ISP intermediate textures (no per-frame alloc), statistics cells once per
# frame, with_before() numbers on the GPU (gpu/build/y2s75-libmerge4/libmerge4.diff). Same 101 exports as libmerge3.
# Nord2Metal is not changed. The library it replaces may be the base or any earlier variant (normally libmerge3);
# which one is recorded in receipt.json ('libmerge4': {'previous': sha}) and kept as libredwoodair.dylib.pre-libmerge4 for the undo.
# (from guest-libmerge3.sh / guest-libmerge3-tc.sh; libmerge3's own backup .pre-libmerge3 and receipt record stay, for its undo after ours)
N='libredwoodair.dylib'
NEW='3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
LM3='3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be'
FROM={BASE: ('y2s47 base', None),       # sha: (name, the variant's own backup (holding BASE) that must be there)
      '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': ('y2s66-thumbrc', 'pre-thumbrc'),
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': ('y2s65-cam-stats', 'pre-camstats1'),
      'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': ('y2s63-gnr1', 'pre-gnr1'),
      '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': ('y2s67-libmerge1', None),
      '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': ('y2s70-libmerge2', None),
      LM3: ('y2s72-libmerge3', None)}
NMS={'92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f': 'thumbsync', '59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d': 'vt23',
     '6482590ef28a719de78dfa2eeb91bd33524273f6c6b9218b79d7f3e1fc283b2e': 'thumbstage', '15f8c23ecfe39ee03b95ada9131e235ba846ddc00cde93f0fbd0555773520573': 'thumbfix',
     'd86aede99ec8042a9bd9baa44dce2870a05f9c128ef3446f6dbe650a52c88b55': 'gpuview', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'thumbfix2',
     '8d5d3c67841be0f9a3b09b1e6ff6e84a80e01251e0147593c5f5831fdbca33dc': 'thumbdiag', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'thumbcomp',
     '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'thumbcomp2'}     # the library only replaces code: same exports as libmerge3
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
def mk(path, data):
    with open(path,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(path,0o644); os.chown(path,0,0)
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge4'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
nm=sha(B+'/Nord2Metal')
assert nm in NMS, 'the installed Nord2Metal is none of %s: %s'%(', '.join(NMS.values()), nm)
print('LIBMERGE4_NORD2METAL', NMS[nm], nm[:12], '(not changed)')
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s75-libmerge4 file'
if cur==NEW:
    m=rc.get('libmerge4') or {}
    assert inv[N]==NEW and m.get('previous') in FROM and os.path.exists(BAK) and sha(BAK)==m['previous'], 'half-installed: review'
    print('LIBMERGE4_ALREADY previous', FROM[m['previous']][0], m['previous'][:12]); print('CC_JOB_ADDED y2s75-libmerge4 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in FROM and inv[N]==cur, '%s is neither the y2s47 library nor thumbrc/camstats/gnr1/libmerge1/libmerge2/libmerge3: %s'%(N,cur)
    assert 'libmerge4' not in rc, 'receipt already has a libmerge4 record while the library is not libmerge4: review'
    name, vb = FROM[cur]
    if vb: assert os.path.exists(DB+'/'+N+'.'+vb) and sha(DB+'/'+N+'.'+vb)==BASE, '%s is installed but its backup %s.%s (the y2s47 library) is missing or different: review'%(name,N,vb)
    if cur==LM3 and 'libmerge3' in rc:     # libmerge3's own record and backup must be coherent: they stay, for its undo after ours
        m3=rc['libmerge3']; b3=DB+'/'+N+'.pre-libmerge3'
        assert m3.get('installed')==LM3 and (not os.path.exists(b3) or sha(b3)==m3.get('previous')), 'libmerge3 record/backup incoherent: review'
    print('LIBMERGE4_FROM', name, cur[:12], ('(its own backup %s.%s stays, for its undo after ours)'%(N,vb)) if vb else '(its own backup/record, if any, stay)')
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there: %s'%(os.path.relpath(BAK,R), sha(BAK))
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('LIBMERGE4_INSTALLED', N, cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R))
    inv[N]=NEW
    rc['inventory'][REL]=inv; rc['libmerge4']={'previous': cur, 'previous_name': name, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_JOB_ADDED y2s75-libmerge4: libredwoodair.dylib (libmerge3 + pooled ISP textures, stats cells once per frame, NR numbers on the GPU)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'libmerge4.previous', cur[:12])
for d in ('refused', 'kept'):     # where the library saves the modules it refuses, and (y2s42) the compute modules it takes; any process writes
    keep=DB+'/'+d
    if not os.path.isdir(keep): os.mkdir(keep)
    os.chmod(keep,0o1777); os.chown(keep,0,0)
    print('LIBMERGE4_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries')
if os.path.exists(SW):
    by=[n.split('made-by-')[1] for n in sorted(os.listdir(DB)) if n.startswith('gnr1-trace.made-by-')]
    print('LIBMERGE4_GNR1_SWITCH already there', ('made by '+', '.join(by)+(' (ours)' if os.path.exists(MK) else ' (left as is)')) if by else 'NOT made by us (left as is)')
else:
    mk(MK, b'y2s75-libmerge4 created /private/var/db/nord2-gpu-gnr1-trace\n'); mk(SW, b''); print('LIBMERGE4_GNR1_SWITCH created', os.path.relpath(SW,R), 'marker', os.path.relpath(MK,R))
print('LIBMERGE4_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
