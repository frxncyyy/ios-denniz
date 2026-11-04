set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s63-gnr1: libredwoodair.dylib only (y2s63's + GNR1 diagnostics); Nord2Metal must stay y2s63-vt23's.
FILES={'libredwoodair.dylib': ('6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea', 'e3d8eb884d5ecf96ecdb5c1cd1a2bcd94c9d48a05b4988bc58cfd87c1a9a2e90')}
N2M={'59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d': 'y2s63-vt23', '6482590ef28a719de78dfa2eeb91bd33524273f6c6b9218b79d7f3e1fc283b2e': 'y2s64-vt24'}   # both built beside y2s63's air_metal.c (y2s64's is identical)
SW=R+'/private/var/db/nord2-gpu-gnr1-trace'; MK=DB+'/gnr1-trace.made-by-gnr1'
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
RP=DB+'/receipt.json'; bak=lambda n: DB+'/'+n+'.pre-gnr1'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
assert sha(B+'/Nord2Metal') in N2M, 'the installed Nord2Metal is neither y2s63-vt23 nor y2s64-vt24 (this dylib was built beside them): '+sha(B+'/Nord2Metal')
print('GNR1_NORD2METAL', N2M[sha(B+'/Nord2Metal')])
cur={n: sha(B+'/'+n) for n in FILES}
new={n: open('/src/'+n,'rb').read() for n in FILES}
for n in FILES: assert hashlib.sha256(new[n]).hexdigest()==FILES[n][1], 'payload %s is not the y2s63-gnr1 file'%n
if all(cur[n]==FILES[n][1] for n in FILES):
    assert all(inv[n]==FILES[n][1] and sha(bak(n))==FILES[n][0] for n in FILES), 'half-installed: review'
    print('GNR1_ALREADY'); print('CC_JOB_ADDED y2s63-gnr1 already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    for n in FILES:
        assert cur[n]==FILES[n][0] and inv[n]==FILES[n][0], '%s is not the file this was built from (y2s63/y2s47 libredwoodair): %s'%(n,cur[n])
    for n in FILES:
        if os.path.exists(bak(n)): assert sha(bak(n))==FILES[n][0], 'a different backup of %s is already there'%n
        else: put(bak(n), open(B+'/'+n,'rb').read(), B+'/'+n)
        assert sha(bak(n))==FILES[n][0]
    for n in FILES:
        put(B+'/'+n, new[n], B+'/'+n); assert sha(B+'/'+n)==FILES[n][1]
        print('GNR1_INSTALLED', n, FILES[n][0][:12], '->', FILES[n][1][:12], 'backup', os.path.relpath(bak(n),R))
        inv[n]=FILES[n][1]
    print('CC_JOB_ADDED y2s63-gnr1: libredwoodair.dylib (GPU fault decode + failed-dispatch bindings, GNR diagnostics)')
    rc['inventory'][REL]=inv
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the change'
    print('CC_RECEIPT_UPDATED inventory', {n: FILES[n][1][:12] for n in sorted(FILES)})
if os.path.exists(SW): print('GNR1_SWITCH already there', 'made by gnr1' if os.path.exists(MK) else 'NOT made by gnr1 (left as is)')
else:
    mk(MK, b'y2s63-gnr1 created /private/var/db/nord2-gpu-gnr1-trace\n'); mk(SW, b''); print('GNR1_SWITCH created', os.path.relpath(SW,R), 'marker', os.path.relpath(MK,R))
print('GNR1_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in ('Nord2Metal','libredwoodair.dylib')}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
