set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL
OLD='a8ec06c1e63c8bf5cd2d8369c2b31c0d81fafef4ec6dc4d9059453a850bf60da'      # gpu/build/y2s33: what guest-applog.sh replaced
NEW='18eedce50e487242ec62ec901aa8d63fa9f8864e03c0cd1d3bb39b4ff4e58638'      # gpu/build/y2s35-applog
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
exe=B+'/Nord2Metal'; marker=B+'/applog-camera'; bak=R+'/private/var/db/nord2-metal/Nord2Metal.pre-applog'; RP=R+'/private/var/db/nord2-metal/receipt.json'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
cur=sha(exe); assert cur in (OLD,NEW), 'Nord2Metal is neither the y2s33 executable nor the applog one: %s'%cur
# everything but the two entries this undoes must be as the receipt says (the marker may have been removed by hand already)
now=inventory(B); rest=lambda d: {k:v for k,v in d.items() if k not in ('Nord2Metal','applog-camera')}
assert rest(now)==rest(inv), 'the installed bundle differs from its receipt: %r'%sorted(set(rest(now).items())^set(rest(inv).items()))
did=[]
if cur==NEW:
    assert os.path.exists(bak) and sha(bak)==OLD, 'no backup of the y2s33 executable'
    put(exe, open(bak,'rb').read(), exe); did.append('Nord2Metal restored')
assert sha(exe)==OLD
if os.path.exists(marker): os.unlink(marker); did.append('marker removed')
if os.path.exists(bak):
    assert sha(bak)==OLD, 'unexpected backup'
    os.unlink(bak); did.append('backup removed')
print('APPLOG_UNDONE', did or 'nothing to do', 'Nord2Metal', sha(exe)[:12], 'mode %o'%(os.stat(exe).st_mode&0o7777))
print('CC_JOB_ADDED camera app log removed')
want=dict(inv); want['Nord2Metal']=OLD; want.pop('applog-camera',None)
if want!=inv:
    rc['inventory'][REL]=want; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory Nord2Metal ->', OLD[:12], '- applog-camera')
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the undo'
print('APPLOG_BUNDLE', sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
