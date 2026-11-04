set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL
OLD='a8ec06c1e63c8bf5cd2d8369c2b31c0d81fafef4ec6dc4d9059453a850bf60da'      # installed: gpu/build/y2s33 (addon y2s33/y2s34)
NEW='18eedce50e487242ec62ec901aa8d63fa9f8864e03c0cd1d3bb39b4ff4e58638'      # gpu/build/y2s35-applog: the same objects + nord2_applog.c
EMPTY=hashlib.sha256(b'').hexdigest()
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
exe=B+'/Nord2Metal'; marker=B+'/applog-camera'; bak=R+'/private/var/db/nord2-metal/Nord2Metal.pre-applog'; RP=R+'/private/var/db/nord2-metal/receipt.json'
new=open('/src/Nord2Metal','rb').read()
assert hashlib.sha256(new).hexdigest()==NEW, 'payload is not the y2s35-applog executable'
rc=json.load(open(RP)); inv=rc['inventory'][REL]
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur=sha(exe)
if cur==NEW:
    assert inv['Nord2Metal']==NEW and inv.get('applog-camera')==EMPTY and sha(bak)==OLD, 'half-installed: review'
    print('APPLOG_ALREADY', cur[:12]); print('CC_JOB_ADDED camera app log already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    assert cur==OLD and inv['Nord2Metal']==OLD, 'Nord2Metal is not the y2s33 executable this was built from: %s'%cur
    assert not os.path.exists(marker) and 'applog-camera' not in inv, 'marker already there'
    if os.path.exists(bak): assert sha(bak)==OLD, 'a different backup is already there'
    else: put(bak, open(exe,'rb').read(), exe)
    assert sha(bak)==OLD
    put(exe, new, exe); assert sha(exe)==NEW
    put(marker, b'', B+'/diag' if os.path.exists(B+'/diag') else B+'/Info.plist')
    print('APPLOG_INSTALLED Nord2Metal', OLD[:12], '->', sha(exe)[:12], 'mode %o'%(os.stat(exe).st_mode&0o7777), 'marker mode %o'%(os.stat(marker).st_mode&0o7777), 'backup', os.path.relpath(bak,R))
    print('CC_JOB_ADDED camera app log: Nord2Metal with nord2_applog + marker applog-camera')
    inv=dict(inv); inv['Nord2Metal']=NEW; inv['applog-camera']=EMPTY; rc['inventory'][REL]=inv
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_RECEIPT_UPDATED inventory Nord2Metal', OLD[:12], '->', NEW[:12], '+ applog-camera')
print('APPLOG_BUNDLE', sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
