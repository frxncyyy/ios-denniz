set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s72-libmerge3: libredwoodair.dylib only = y2s70-libmerge2 (base + thumbrc + camstats + GNR1 + NR2) + looped GPU stats shader, shaders built
# off the camera thread with a 250 ms bounded wait and CPU fallback, GPU path off after 3 failures (gpu/build/y2s72-libmerge3/libmerge3.diff).
# Nord2Metal is not changed and must be y2s65-thumbsync. The library it replaces may be the base or any of the three variants it merges;
# which one is recorded in receipt.json ('libmerge3': {'previous': sha}) and kept as libredwoodair.dylib.pre-libmerge3 for the undo.
N='libredwoodair.dylib'
NEW='3c71650a788401fdc1ab10ad90b4571ed50f034f6cc47c349ed092bb5b8437be'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
FROM={BASE: ('y2s47 base', None),       # sha: (name, the variant's own backup (holding BASE) that must be there)
      '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': ('y2s66-thumbrc', 'pre-thumbrc'),
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': ('y2s65-cam-stats', 'pre-camstats1'),
      'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': ('y2s63-gnr1', 'pre-gnr1'),
      '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': ('y2s67-libmerge1', None),
      '196e1dcda8314ff5c3d54458fd5ae0d5a96359a2b871e5d4e2aa53d1b3197fed': ('y2s70-libmerge2', None)}     # (y2s72: libmerge2 too; its own backup .pre-libmerge2 and receipt record stay, for its undo after ours)     # (y2s72: libmerge1 too; its own backup and record stay, for its undo after ours)
NMS=['92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f', '59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d', '6482590ef28a719de78dfa2eeb91bd33524273f6c6b9218b79d7f3e1fc283b2e', '15f8c23ecfe39ee03b95ada9131e235ba846ddc00cde93f0fbd0555773520573', 'd86aede99ec8042a9bd9baa44dce2870a05f9c128ef3446f6dbe650a52c88b55']      # (y2s72: Nord2Metal y2s65-thumbsync, y2s63-vt23, y2s64-thumbstage, y2s73-thumbfix or y2s71-gpuview (both vt23 + one Nord2Metal-side change): the library only adds exports)
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-libmerge3'; MKG=DB+'/gnr1-trace.made-by-gnr1'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge3'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
assert sha(B+'/Nord2Metal') in NMS, 'the installed Nord2Metal is none of thumbsync, vt23, thumbstage, thumbfix, gpuview: '+sha(B+'/Nord2Metal')
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s72-libmerge3 file'
if cur==NEW:
    m=rc.get('libmerge3') or {}
    assert inv[N]==NEW and m.get('previous') in FROM and os.path.exists(BAK) and sha(BAK)==m['previous'], 'half-installed: review'
    print('LIBMERGE3_ALREADY previous', FROM[m['previous']][0], m['previous'][:12]); print('CC_JOB_ADDED y2s72-libmerge3 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in FROM and inv[N]==cur, '%s is neither the y2s47 library nor thumbrc/camstats/gnr1/libmerge1/libmerge2: %s'%(N,cur)
    assert 'libmerge3' not in rc, 'receipt already has a libmerge3 record while the library is not libmerge3: review'
    name, vb = FROM[cur]
    if vb: assert os.path.exists(DB+'/'+N+'.'+vb) and sha(DB+'/'+N+'.'+vb)==BASE, '%s is installed but its backup %s.%s (the y2s47 library) is missing or different: review'%(name,N,vb)
    print('LIBMERGE3_FROM', name, cur[:12], ('(its own backup %s.%s stays, for its undo after ours)'%(N,vb)) if vb else '')
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there: %s'%(os.path.relpath(BAK,R), sha(BAK))
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('LIBMERGE3_INSTALLED', N, cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R))
    inv[N]=NEW
    rc['inventory'][REL]=inv; rc['libmerge3']={'previous': cur, 'previous_name': name, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_JOB_ADDED y2s72-libmerge3: libredwoodair.dylib (libmerge2 NR2 + looped GPU stats shader, bounded shader build with CPU fallback)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'libmerge3.previous', cur[:12])
for d in ('refused', 'kept'):     # where the library saves the modules it refuses, and (y2s42) the compute modules it takes; any process writes
    keep=DB+'/'+d
    if not os.path.isdir(keep): os.mkdir(keep)
    os.chmod(keep,0o1777); os.chown(keep,0,0)
    print('LIBMERGE3_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries')
if os.path.exists(SW): print('LIBMERGE3_GNR1_SWITCH already there', 'made by libmerge3' if os.path.exists(MK) else 'made by gnr1 (left as is)' if os.path.exists(MKG) else 'NOT made by us (left as is)')
else:
    mk(MK, b'y2s72-libmerge3 created /private/var/db/nord2-gpu-gnr1-trace\n'); mk(SW, b''); print('LIBMERGE3_GNR1_SWITCH created', os.path.relpath(SW,R), 'marker', os.path.relpath(MK,R))
print('LIBMERGE3_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
