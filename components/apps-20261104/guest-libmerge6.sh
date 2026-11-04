set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s75/lm6 libmerge6: libredwoodair.dylib only = libmerge5 plus the ISP pass made once per DIFFERENT sensor column and row when the picture is
# larger than the sensor cells it shows (isp_expand; gpu/build/y2s75-libmerge4/lm6/libmerge6-vs-libmerge5.diff). OFF while
# /private/var/db/nord2-isp-noexpand exists (guest-ispnoexpand.sh) or RWA_ISP_NOEXPAND is set; fold/r32/mrt as libmerge5 (nord2-isp-features).
# Nord2Metal is not changed. The library it replaces may be the base or any earlier variant (normally libmerge5);
# which one is recorded in receipt.json ('libmerge6': {'previous': sha}) and kept as libredwoodair.dylib.pre-libmerge6 for the undo.
# (from guest-libmerge5.sh; libmerge5's/libmerge4's/libmerge3's own backups .pre-libmerge5/.pre-libmerge4/.pre-libmerge3 and receipt records
# stay, for their undo after ours)
N='libredwoodair.dylib'
NEW='fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
LM3='3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be'
LM4='3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0'
LM5='1eae024e2382da3086eb942c9e9bfc010e45560afac809feedfaeb972e3f2908'
FROM={BASE: ('y2s47 base', None),       # sha: (name, the variant's own backup (holding BASE) that must be there)
      '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': ('y2s66-thumbrc', 'pre-thumbrc'),
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': ('y2s65-cam-stats', 'pre-camstats1'),
      'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': ('y2s63-gnr1', 'pre-gnr1'),
      '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': ('y2s67-libmerge1', None),
      '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': ('y2s70-libmerge2', None),
      LM3: ('y2s72-libmerge3', None),
      LM4: ('y2s75-libmerge4', None),
      LM5: ('y2s75-libmerge5', None)}
NMS={'92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f': 'thumbsync', '59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d': 'vt23',
     '6482590ef28a719de78dfa2eeb91bd33524273f6c6b9218b79d7f3e1fc283b2e': 'thumbstage', '15f8c23ecfe39ee03b95ada9131e235ba846ddc00cde93f0fbd0555773520573': 'thumbfix',
     'd86aede99ec8042a9bd9baa44dce2870a05f9c128ef3446f6dbe650a52c88b55': 'gpuview', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'thumbfix2',
     '8d5d3c67841be0f9a3b09b1e6ff6e84a80e01251e0147593c5f5831fdbca33dc': 'thumbdiag', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'thumbcomp',
     '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'thumbcomp2', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'thumbcomp3',
     '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'thumbcomp4',
     'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'thumbcomp5'}
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-libmerge6'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge6'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
nm=sha(B+'/Nord2Metal')
assert nm in NMS, 'the installed Nord2Metal is none of %s: %s'%(', '.join(NMS.values()), nm)
print('LIBMERGE6_NORD2METAL', NMS[nm], nm[:12], '(not changed)')
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the libmerge6 file'
if cur==NEW:
    m=rc.get('libmerge6') or {}
    assert inv[N]==NEW and m.get('previous') in FROM and os.path.exists(BAK) and sha(BAK)==m['previous'], 'half-installed: review'
    print('LIBMERGE6_ALREADY previous', FROM[m['previous']][0], m['previous'][:12]); print('CC_JOB_ADDED libmerge6 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in FROM and inv[N]==cur, '%s is neither the y2s47 library nor thumbrc/camstats/gnr1/libmerge1/libmerge2/libmerge3/libmerge4/libmerge5: %s'%(N,cur)
    assert 'libmerge6' not in rc, 'receipt already has a libmerge6 record while the library is not libmerge6: review'
    name, vb = FROM[cur]
    if vb: assert os.path.exists(DB+'/'+N+'.'+vb) and sha(DB+'/'+N+'.'+vb)==BASE, '%s is installed but its backup %s.%s (the y2s47 library) is missing or different: review'%(name,N,vb)
    for lm, sh in (('libmerge5', LM5), ('libmerge4', LM4), ('libmerge3', LM3)):     # the installed variant's own record and backup must be coherent: they stay, for its undo after ours
        if cur==sh and lm in rc:
            mr=rc[lm]; bk=DB+'/'+N+'.pre-'+lm
            assert mr.get('installed')==sh and (not os.path.exists(bk) or sha(bk)==mr.get('previous')), '%s record/backup incoherent: review'%lm
    print('LIBMERGE6_FROM', name, cur[:12], ('(its own backup %s.%s stays, for its undo after ours)'%(N,vb)) if vb else '(its own backup/record, if any, stay)')
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there: %s'%(os.path.relpath(BAK,R), sha(BAK))
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('LIBMERGE6_INSTALLED', N, cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R))
    inv[N]=NEW
    rc['inventory'][REL]=inv; rc['libmerge6']={'previous': cur, 'previous_name': name, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_JOB_ADDED libmerge6: libredwoodair.dylib (libmerge5 + ISP pass once per sensor place shown, off by /private/var/db/nord2-isp-noexpand)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'libmerge6.previous', cur[:12])
for d in ('refused', 'kept'):     # where the library saves the modules it refuses, and (y2s42) the compute modules it takes; any process writes
    keep=DB+'/'+d
    if not os.path.isdir(keep): os.mkdir(keep)
    os.chmod(keep,0o1777); os.chown(keep,0,0)
    print('LIBMERGE6_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries')
if os.path.exists(SW):
    by=[n.split('made-by-')[1] for n in sorted(os.listdir(DB)) if n.startswith('gnr1-trace.made-by-')]
    print('LIBMERGE6_GNR1_SWITCH already there', ('made by '+', '.join(by)+(' (ours)' if os.path.exists(MK) else ' (left as is)')) if by else 'NOT made by us (left as is)')
else:
    mk(MK, b'libmerge6 created /private/var/db/nord2-gpu-gnr1-trace\n'); mk(SW, b''); print('LIBMERGE6_GNR1_SWITCH created', os.path.relpath(SW,R), 'marker', os.path.relpath(MK,R))
FT=R+'/private/var/db/nord2-isp-features'; NX=R+'/private/var/db/nord2-isp-noexpand'
print('LIBMERGE6_ISP_FEATURES', repr(open(FT,'rb').read()[:64]) if os.path.isfile(FT) else 'none (fold, r32, mrt off)')
print('LIBMERGE6_ISP_NOEXPAND', 'there: the new pass is OFF' if os.path.lexists(NX) else 'none (the new pass is on)')
print('LIBMERGE6_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
