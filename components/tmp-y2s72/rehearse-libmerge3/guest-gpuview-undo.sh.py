import hashlib, json, os
R='$NORD2_WORK/gpu/build/tmp-y2s72/rehearse-libmerge3/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# y2s71-gpuview over vt23 / thumbstage / thumbsync (whichever is installed; it is kept and restored by the undo).
N='Nord2Metal'; NEW='d86aede99ec8042a9bd9baa44dce2870a05f9c128ef3446f6dbe650a52c88b55'
OLDS={'59212d387b5400b8ef524f36c6b437e67e4b2390e9c222c77acf62c621954a1d': 'y2s63-vt23'}
LIBS={'6df1af45106200dbbb27ae9b132667e650c6a511049ae95e9965d581207798ea': 'y2s47', '616612f55baa0a02078accfdc499783f6c80e79e4c5fd37acd03176b3212f49d': 'libmerge1', '35cc638b15efc9a8c45599f73989d0302182cf7ae5fb3de98d669a8a64966d52': 'thumbrc'}
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
RP=DB+'/receipt.json'; BAK=DB+'/Nord2Metal.pre-gpuview'
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
lib=sha(B+'/libredwoodair.dylib'); assert lib in LIBS, 'unknown libredwoodair.dylib '+lib
cur=sha(B+'/'+N)
did=[]
if cur==NEW:
    assert os.path.exists(BAK) and sha(BAK) in OLDS, 'no usable backup of the earlier Nord2Metal'
    old=open(BAK,'rb').read(); put(B+'/'+N, old, B+'/'+N); did.append('Nord2Metal restored to '+OLDS[sha(BAK)])
    inv[N]=sha(B+'/'+N); rc['inventory'][REL]=inv; put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    os.unlink(BAK); did.append('backup removed')
    print('GPUVIEW_UNDONE', did); print('CC_JOB_ADDED y2s71-gpuview removed'); print('CC_RECEIPT_UPDATED inventory', {N: inv[N][:12]})
else:
    assert cur in OLDS, 'the installed Nord2Metal is neither y2s71-gpuview nor a known earlier one: '+cur
    if os.path.exists(BAK): assert sha(BAK)==cur, 'unexpected backup'; os.unlink(BAK); did.append('stale backup removed')
    print('GPUVIEW_UNDONE', did or 'nothing to do'); print('CC_JOB_ADDED y2s71-gpuview removed'); print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('GPUVIEW_BUNDLE', sha(B+'/'+N)[:12], sorted(os.listdir(B)))
os.sync()
