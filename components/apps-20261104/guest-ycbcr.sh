set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL
OLD='434d382ba98f47b3c5581f2076eb8e66b6c87cc9653eb256e1f5a9bae8014969'      # gpu/build/y2s36-planar (installed by guest-planar.sh)
NEW='98255767d9bcc697ceadbd1f416ad79f3c88d1237d2c2796848b0ce2aabae10d'      # gpu/build/y2s37-ycbcr: y2s36-planar + format 500 served as converted RGB
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
exe=B+'/Nord2Metal'; bak=R+'/private/var/db/nord2-metal/Nord2Metal.pre-ycbcr'; RP=R+'/private/var/db/nord2-metal/receipt.json'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur=sha(exe)
new=open('/src/Nord2Metal','rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s37-ycbcr executable'
if cur==NEW:
    assert inv['Nord2Metal']==NEW and sha(bak)==OLD, 'half-installed: review'
    print('YCBCR_ALREADY', cur[:12]); print('CC_JOB_ADDED Nord2Metal y2s37-ycbcr already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur==OLD and inv['Nord2Metal']==OLD, 'Nord2Metal is not the y2s36-planar executable this was built from: %s'%cur
    if os.path.exists(bak): assert sha(bak)==OLD, 'a different backup is already there'
    else: put(bak, open(exe,'rb').read(), exe)
    assert sha(bak)==OLD
    put(exe, new, exe); assert sha(exe)==NEW
    print('YCBCR_INSTALLED Nord2Metal', OLD[:12], '->', sha(exe)[:12], 'mode %o'%(os.stat(exe).st_mode&0o7777), 'backup', os.path.relpath(bak,R))
    print('CC_JOB_ADDED Nord2Metal y2s37-ycbcr (YCbCr surfaces served as RGB)')
    inv['Nord2Metal']=NEW; rc['inventory'][REL]=inv
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_RECEIPT_UPDATED inventory Nord2Metal', OLD[:12], '->', NEW[:12])
print('YCBCR_BUNDLE', sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
