set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# name: (installed before = the y2s38 library, y2s39-gpuisp)
FILES={'libredwoodair.dylib': ('7afe286b2ee1c30ef39c5a5197c5fb83c8ddc93d90ad4c2635f9f55498cad48c', 'a4fa9b4b6c492b174721e86b5a3626d84f20e0e55c1e62da2769489186fdfe33')}
KEEP={'Nord2Metal': 'f2c5d167e0c55c060d0b5950c3a31bd4871255c84e2ba9619ebaffed5f09949b'}      # not touched, and must be the y2s38 one this library was made beside
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
RP=DB+'/receipt.json'; bak=lambda n: DB+'/'+n+'.pre-gpuisp'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur={n: sha(B+'/'+n) for n in FILES}
for n in KEEP: assert sha(B+'/'+n)==KEEP[n], '%s is not the y2s38 one: %s'%(n,sha(B+'/'+n))
new={n: open('/src/'+n,'rb').read() for n in FILES}
for n in FILES: assert hashlib.sha256(new[n]).hexdigest()==FILES[n][1], 'payload %s is not the y2s39-gpuisp file'%n
if all(cur[n]==FILES[n][1] for n in FILES):
    assert all(inv[n]==FILES[n][1] and sha(bak(n))==FILES[n][0] for n in FILES), 'half-installed: review'
    print('GPUISP_ALREADY'); print('CC_JOB_ADDED y2s39-gpuisp already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    for n in FILES:
        assert cur[n]==FILES[n][0] and inv[n]==FILES[n][0], '%s is not the file this was built from (the y2s38 library): %s'%(n,cur[n])
    for n in FILES:         # every backup first, then every replacement
        if os.path.exists(bak(n)): assert sha(bak(n))==FILES[n][0], 'a different backup of %s is already there'%n
        else: put(bak(n), open(B+'/'+n,'rb').read(), B+'/'+n)
        assert sha(bak(n))==FILES[n][0]
    for n in FILES:
        put(B+'/'+n, new[n], B+'/'+n); assert sha(B+'/'+n)==FILES[n][1]
        print('GPUISP_INSTALLED', n, FILES[n][0][:12], '->', FILES[n][1][:12], 'backup', os.path.relpath(bak(n),R))
        inv[n]=FILES[n][1]
    print('CC_JOB_ADDED y2s39-gpuisp: libredwoodair.dylib with the rwa_isp_* calls (the camera ISP on the GPU)')
    rc['inventory'][REL]=inv
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_RECEIPT_UPDATED inventory', {n: FILES[n][1][:12] for n in sorted(FILES)})
print('GPUISP_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in sorted(FILES)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
