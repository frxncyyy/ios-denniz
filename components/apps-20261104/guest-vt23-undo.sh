set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# name: (installed before = y2s62-vt22's Nord2Metal, y2s63-vt23).  libredwoodair.dylib is not changed: y2s63's is y2s47's, byte for byte.
FILES={'Nord2Metal': ('dc40f3eb7fa8adf4ef52ded4936a2d4c07cd69774054df80af6e1dbce2365ae8', '59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d')}
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
RP=DB+'/receipt.json'; bak=lambda n: DB+'/'+n+'.pre-vt23'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur={n: sha(B+'/'+n) for n in FILES}
assert sha(B+'/libredwoodair.dylib')==LIB, 'the installed libredwoodair.dylib is not the y2s47 one this Nord2Metal was built beside: '+sha(B+'/libredwoodair.dylib')
for n in FILES: assert cur[n] in FILES[n], '%s is neither the earlier file nor the y2s63-vt23 one: %s'%(n,cur[n])
for n in FILES:
    if cur[n]==FILES[n][1]: assert os.path.exists(bak(n)) and sha(bak(n))==FILES[n][0], 'no backup of the earlier %s'%n
    if os.path.exists(bak(n)): assert sha(bak(n))==FILES[n][0], 'unexpected backup of %s'%n
did=[]
for n in FILES:
    if cur[n]==FILES[n][1]:
        put(B+'/'+n, open(bak(n),'rb').read(), B+'/'+n); did.append(n+' restored')
    assert sha(B+'/'+n)==FILES[n][0]
for n in FILES:
    if os.path.exists(bak(n)): os.unlink(bak(n)); did.append(n+' backup removed')
print('VT23_UNDONE', did or 'nothing to do')
print('CC_JOB_ADDED y2s63-vt23 removed')
want=dict(inv)
for n in FILES: want[n]=FILES[n][0]
if want!=inv:
    rc['inventory'][REL]=want; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory', {n: FILES[n][0][:12] for n in sorted(FILES)})
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the undo'
print('VT23_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in sorted(FILES)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
