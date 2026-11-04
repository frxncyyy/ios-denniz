set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# name: (installed before = the y2s47 library, y2s66-thumbrc's).  Nord2Metal is not changed (y2s65-thumbsync).
FILES={'libredwoodair.dylib': ('6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52')}
NM='92e85b54d94bc256036b51adaafc422855d0f027f441de70ae8a541dbf7e0f9f'
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
RP=DB+'/receipt.json'; bak=lambda n: DB+'/'+n+'.pre-thumbrc'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
cur={n: sha(B+'/'+n) for n in FILES}
assert sha(B+'/Nord2Metal')==NM, 'the installed Nord2Metal is not y2s65-thumbsync, the one this library was tested beside: '+sha(B+'/Nord2Metal')
for n in FILES: assert cur[n] in FILES[n], '%s is neither the earlier file nor the y2s66-thumbrc one: %s'%(n,cur[n])
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
print('THUMBRC_UNDONE', did or 'nothing to do')
print('CC_JOB_ADDED y2s66-thumbrc removed')
want=dict(inv)
for n in FILES: want[n]=FILES[n][0]
if want!=inv:
    rc['inventory'][REL]=want; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory', {n: FILES[n][0][:12] for n in sorted(FILES)})
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL], 'receipt does not match the bundle after the undo'
print('THUMBRC_BUNDLE', {n: (sha(B+'/'+n)[:12], 'mode %o'%(os.stat(B+'/'+n).st_mode&0o7777)) for n in sorted(FILES)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
