set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# thumbgrid1 = y2s91-thumbgrid: Nord2Metal (thumbfix5 + rw_tg_*: exact odd-size YCbCr pass, follow-live with the kernel use count,
# write-in-flight deferral, render-target seed touch, CPU-reference CHECK 'NORD2 PERF PHOTOS CHECK', buffer/texture pools, thumbnail
# cache, deferred dispatches) + libredwoodair.dylib (libmerge6 + rwa_texture_convert_ycbcr_exact, rwa_dispatch_buffers_deferred,
# rwa_buffer/texture_recycle, UBO tail zeroing). Over thumbfix5 + libmerge6 only (both kept and restored by the undo). Userland only:
# any kernel; follow-live needs 0398 (else the thumbfix5 freeze rule stays). Switches: guest-thumbgrid1-flags.sh.
N='Nord2Metal'; L='libredwoodair.dylib'
NEW={N: '5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf', L: '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd'}
OLD={N: 'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f', L: 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc'}
NAMES={N: 'y2s83-thumbfix5', L: 'libmerge6'}
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
data={f: open('/src/'+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the y2s91-thumbgrid one'%f
cur={f: sha(B+'/'+f) for f in (N, L)}
if cur==NEW:
    assert 'thumbgrid1' in rc and all(os.path.exists(BAK[f]) and sha(BAK[f])==OLD[f] for f in (N, L)), 'half-installed: review'
    print('THUMBGRID1_ALREADY'); print('CC_JOB_ADDED thumbgrid1 (y2s91-thumbgrid Nord2Metal + library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    for f in (N, L):
        assert cur[f]==OLD[f] and inv[f]==cur[f], 'the installed %s is not %s (%s): thumbgrid1 goes over thumbfix5 + libmerge6 only'%(f, NAMES[f], cur[f])
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'a different backup %s is already there'%BAK[f]
        else: put(BAK[f], open(B+'/'+f,'rb').read(), B+'/'+f)
    for f in (L, N):
        put(B+'/'+f, data[f], B+'/'+f); assert sha(B+'/'+f)==NEW[f]
        inv[f]=NEW[f]
    rc['inventory'][REL]=inv; rc['thumbgrid1']={'previous': OLD, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('THUMBGRID1_INSTALLED', {f: cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'backups', [os.path.relpath(BAK[f],R) for f in (N, L)])
    print('CC_JOB_ADDED thumbgrid1: Nord2Metal + libredwoodair = y2s91-thumbgrid (exact YCbCr pass, follow-live, CHECK, pools, async dispatch)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in (N, L)}, 'record thumbgrid1')
for f in sorted(os.listdir(DB)):
    if 'thumbgrid' in f and not f.endswith('.pre-thumbgrid1'): print('THUMBGRID1_SWITCH', f, 'present')
print('THUMBGRID1_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
