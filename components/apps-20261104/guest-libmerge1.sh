set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s67-libmerge1: libredwoodair.dylib only = y2s47 base 6df1af45 + thumbrc + camstats + GNR1 (gpu/build/y2s67-libmerge1/libmerge1.diff).
# Nord2Metal is not changed and must be y2s65-thumbsync. The library it replaces may be the base or any of the three variants it merges;
# which one is recorded in receipt.json ('libmerge1': {'previous': sha}) and kept as libredwoodair.dylib.pre-libmerge1 for the undo.
N='libredwoodair.dylib'
NEW='616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
FROM={BASE: ('y2s47 base', None),       # sha: (name, the variant's own backup (holding BASE) that must be there)
      '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': ('y2s66-thumbrc', 'pre-thumbrc'),
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': ('y2s65-cam-stats', 'pre-camstats1'),
      'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': ('y2s63-gnr1', 'pre-gnr1')}
NM='92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f'
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-libmerge1'; MKG=DB+'/gnr1-trace.made-by-gnr1'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
assert sha(B+'/Nord2Metal')==NM, 'the installed Nord2Metal is not y2s65-thumbsync, the one this library was built and checked beside: '+sha(B+'/Nord2Metal')
cur=sha(B+'/'+N)
new=open('/src/'+N,'rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s67-libmerge1 file'
if cur==NEW:
    m=rc.get('libmerge1') or {}
    assert inv[N]==NEW and m.get('previous') in FROM and os.path.exists(BAK) and sha(BAK)==m['previous'], 'half-installed: review'
    print('LIBMERGE1_ALREADY previous', FROM[m['previous']][0], m['previous'][:12]); print('CC_JOB_ADDED y2s67-libmerge1 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur in FROM and inv[N]==cur, '%s is neither the y2s47 library nor thumbrc/camstats/gnr1 (the libraries this merges): %s'%(N,cur)
    assert 'libmerge1' not in rc, 'receipt already has a libmerge1 record while the library is not libmerge1: review'
    name, vb = FROM[cur]
    if vb: assert os.path.exists(DB+'/'+N+'.'+vb) and sha(DB+'/'+N+'.'+vb)==BASE, '%s is installed but its backup %s.%s (the y2s47 library) is missing or different: review'%(name,N,vb)
    print('LIBMERGE1_FROM', name, cur[:12], ('(its own backup %s.%s stays, for its undo after ours)'%(N,vb)) if vb else '')
    if os.path.exists(BAK): assert sha(BAK)==cur, 'a different backup %s is already there: %s'%(os.path.relpath(BAK,R), sha(BAK))
    else: put(BAK, open(B+'/'+N,'rb').read(), B+'/'+N)
    assert sha(BAK)==cur
    put(B+'/'+N, new, B+'/'+N); assert sha(B+'/'+N)==NEW
    print('LIBMERGE1_INSTALLED', N, cur[:12], '->', NEW[:12], 'backup', os.path.relpath(BAK,R))
    inv[N]=NEW
    rc['inventory'][REL]=inv; rc['libmerge1']={'previous': cur, 'previous_name': name, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_JOB_ADDED y2s67-libmerge1: libredwoodair.dylib (thumbrc one import per screen + camstats GPU frame statistics + GNR1 fault/dispatch diagnostics)')
    print('CC_RECEIPT_UPDATED inventory', {N: NEW[:12]}, 'libmerge1.previous', cur[:12])
for d in ('refused', 'kept'):     # where the library saves the modules it refuses, and (y2s42) the compute modules it takes; any process writes
    keep=DB+'/'+d
    if not os.path.isdir(keep): os.mkdir(keep)
    os.chmod(keep,0o1777); os.chown(keep,0,0)
    print('LIBMERGE1_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries')
if os.path.exists(SW): print('LIBMERGE1_GNR1_SWITCH already there', 'made by libmerge1' if os.path.exists(MK) else 'made by gnr1 (left as is)' if os.path.exists(MKG) else 'NOT made by us (left as is)')
else:
    mk(MK, b'y2s67-libmerge1 created /private/var/db/nord2-gpu-gnr1-trace\n'); mk(SW, b''); print('LIBMERGE1_GNR1_SWITCH created', os.path.relpath(SW,R), 'marker', os.path.relpath(MK,R))
print('LIBMERGE1_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
