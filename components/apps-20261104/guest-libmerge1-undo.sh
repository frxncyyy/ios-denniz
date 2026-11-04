set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# Undo of y2s67-libmerge1: libredwoodair.dylib back to the library it replaced (receipt 'libmerge1'.previous: the y2s47 base 6df1af45,
# or thumbrc/camstats/gnr1 if one of them was installed then; their own backups were left alone). /src/libredwoodair.dylib is the base
# 6df1af45, used only when the record says the base and the backup is gone.
N='libredwoodair.dylib'
NEW='616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d'
BASE='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
FROM={BASE: 'y2s47 base', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': 'y2s66-thumbrc',
      '0f7c4bad29f85b12c8a173dfde03e6014c117657ed8ce771764df6b5ad31a1b0': 'y2s65-cam-stats', 'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90': 'y2s63-gnr1'}
NM='92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f'
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-libmerge1'
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
RP=DB+'/receipt.json'; BAK=DB+'/'+N+'.pre-libmerge1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
assert sha(B+'/Nord2Metal')==NM, 'the installed Nord2Metal is not y2s65-thumbsync: '+sha(B+'/Nord2Metal')
assert hashlib.sha256(open('/src/'+N,'rb').read()).hexdigest()==BASE, 'undo payload is not the y2s47 library'
cur=sha(B+'/'+N); m=rc.get('libmerge1')
did=[]
if cur==NEW:
    assert m and m.get('previous') in FROM and m.get('installed')==NEW, 'libmerge1 is installed but the receipt has no usable libmerge1 record: review'
    prev=m['previous']
    if os.path.exists(BAK):
        assert sha(BAK)==prev, 'backup %s is not the recorded previous library: %s'%(os.path.relpath(BAK,R), sha(BAK))
        data=open(BAK,'rb').read()
    else:
        assert prev==BASE, 'no backup of the previous library (%s) and it is not the base: review'%FROM[prev]
        data=open('/src/'+N,'rb').read(); did.append('backup missing: base taken from the undo payload')
    put(B+'/'+N, data, B+'/'+N); assert sha(B+'/'+N)==prev
    did.append('%s restored to %s %s'%(N, FROM[prev], prev[:12]))
else:
    assert cur in FROM and not m and not (os.path.exists(BAK) and sha(BAK)!=cur), '%s is not libmerge1 and not a clean earlier state: %s'%(N,cur)
    prev=cur
if os.path.exists(BAK): os.unlink(BAK); did.append('backup removed')
if os.path.exists(MK):
    if os.path.exists(SW): os.unlink(SW); did.append('gnr1 trace switch removed')
    os.unlink(MK); did.append('marker removed')
print('LIBMERGE1_UNDONE', did or 'nothing to do')
print('CC_JOB_ADDED y2s67-libmerge1 removed')
want=dict(inv); want[N]=prev
if want!=inv or 'libmerge1' in rc:
    rc['inventory'][REL]=want; rc.pop('libmerge1', None); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory', {N: prev[:12]}, 'libmerge1 record removed')
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the undo'
print('LIBMERGE1_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal', N)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
