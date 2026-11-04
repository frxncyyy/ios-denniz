set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of thumbgrid1: the backed-up Nord2Metal (thumbfix5) and libredwoodair.dylib (libmerge6) back, the receipt record thumbgrid1
# removed, and every switch flag made by guest-thumbgrid1-flags.sh removed.
N='Nord2Metal'; L='libredwoodair.dylib'
NEW={N: '5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf', L: '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd'}
OLD={N: 'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f', L: 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc'}
ALLOWED=['no-thumbgrid']+['no-thumbgrid-'+n for n in ('exact','live','pending','rttouch','check','bufpool','texpool','thumbcache','async','ubo','log')]+['thumbgrid-pools-all','thumbgrid-async-all','thumbgrid-check-all','thumbgrid-rttouch-all']
MARK=b'made-by-thumbgrid1\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-thumbgrid1' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
did=[]
for n in ALLOWED:
    p=DB+'/'+n
    if os.path.isfile(p) and open(p,'rb').read()==MARK: os.unlink(p); did.append(n+' flag removed')
cur={f: sha(B+'/'+f) for f in (N, L)}
changed=False
for f in (N, L):
    if cur[f]==NEW[f]:
        assert os.path.exists(BAK[f]) and sha(BAK[f])==OLD[f], 'no usable backup of the earlier '+f
        put(B+'/'+f, open(BAK[f],'rb').read(), B+'/'+f); inv[f]=OLD[f]; changed=True; did.append(f+' restored')
    else:
        assert cur[f]==OLD[f], 'the installed %s is neither thumbgrid1 nor the one it replaced: %s'%(f, cur[f])
for f in (N, L):
    if os.path.exists(BAK[f]): assert sha(BAK[f])==OLD[f], 'unexpected backup '+BAK[f]; os.unlink(BAK[f]); did.append(f+' backup removed')
if changed or 'thumbgrid1' in rc:
    rc['inventory'][REL]=inv; rc.pop('thumbgrid1', None); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in (N, L)}, 'record thumbgrid1 removed')
else:
    print('CC_RECEIPT_UPDATED unchanged')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('THUMBGRID1_UNDONE', did or 'nothing to do'); print('CC_JOB_ADDED thumbgrid1 (y2s91-thumbgrid) removed')
print('THUMBGRID1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
