set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# name: (installed before = y2s60-vt20's Nord2Metal, y2s61-vt21).  libredwoodair.dylib is not changed: y2s61's is y2s47's, byte for byte.
FILES={'Nord2Metal': ('e1f82e2034beaeb82e91fde377db24c71ee1ee0a457dd35a225812238c50ed9e', '947f50db9b316fd40fc28d84b6080e3e5098f3308f16922677e1cc9f61a388cd')}
LIB='6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea'
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
RP=DB+'/receipt.json'; bak=lambda n: DB+'/'+n+'.pre-vt21'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur={n: sha(B+'/'+n) for n in FILES}
assert sha(B+'/libredwoodair.dylib')==LIB, 'the installed libredwoodair.dylib is not the y2s47 one this Nord2Metal was built beside: '+sha(B+'/libredwoodair.dylib')
new={n: open('/src/'+n,'rb').read() for n in FILES}
for n in FILES: assert hashlib.sha256(new[n]).hexdigest()==FILES[n][1], 'payload %s is not the y2s61-vt21 file'%n
if all(cur[n]==FILES[n][1] for n in FILES):
    assert all(inv[n]==FILES[n][1] and sha(bak(n))==FILES[n][0] for n in FILES), 'half-installed: review'
    print('VT21_ALREADY'); print('CC_JOB_ADDED y2s61-vt21 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    for n in FILES:
        assert cur[n]==FILES[n][0] and inv[n]==FILES[n][0], '%s is not the file this was built from (the y2s60-vt20 Nord2Metal): %s'%(n,cur[n])
    for n in FILES:         # every backup first, then every replacement
        if os.path.exists(bak(n)): assert sha(bak(n))==FILES[n][0], 'a different backup of %s is already there'%n
        else: put(bak(n), open(B+'/'+n,'rb').read(), B+'/'+n)
        assert sha(bak(n))==FILES[n][0]
    for n in FILES:
        put(B+'/'+n, new[n], B+'/'+n); assert sha(B+'/'+n)==FILES[n][1]
        print('VT21_INSTALLED', n, FILES[n][0][:12], '->', FILES[n][1][:12], 'backup', os.path.relpath(bak(n),R))
        inv[n]=FILES[n][1]
    print('CC_JOB_ADDED y2s61-vt21: Nord2Metal (cache flush before GPU YCbCr conversion, cpu-view log)')
    rc['inventory'][REL]=inv
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_RECEIPT_UPDATED inventory', {n: FILES[n][1][:12] for n in sorted(FILES)})
for d in ('refused', 'kept'):     # where the library saves the modules it refuses, and (y2s42) the compute modules it takes; any process writes
    keep=DB+'/'+d
    if not os.path.isdir(keep): os.mkdir(keep)
    os.chmod(keep,0o1777); os.chown(keep,0,0)
    print('VT21_KEEP_DIR', os.path.relpath(keep,R), 'mode %o'%(os.stat(keep).st_mode&0o7777), len(os.listdir(keep)), 'entries', sorted(os.listdir(keep))[:6])
print('VT21_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in sorted(FILES)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
