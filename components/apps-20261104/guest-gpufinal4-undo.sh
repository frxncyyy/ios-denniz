set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gpufinal4 (y2s94-final Nord2Metal + y2s91-thumbgrid library): each file gpufinal4 replaced is put back exactly as the receipt
# record 'gpufinal4' says (previous), from its backup <file>.pre-gpufinal4; if a backup is missing, the fallback shipped in this payload
# dir (Nord2Metal = y2s83-thumbfix5 f90a091b, libredwoodair.dylib = libmerge6 fe59bddb) is used only when it is exactly that previous
# file (or, with no record at all, as the known-good thumbfix5 + libmerge6 pair). A library that was already the thumbgrid library
# before gpufinal4 stays (always so over gpufinal2, whose Nord2Metal be7a746d comes back from its backup; gpufinal2's own
# record and .pre-gpufinal2 backups are not touched, so guest-gpufinal2-undo.sh works afterwards). Backups and record removed; switch files with the marker 'made-by-gpufinal4' removed, all others kept.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: '015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f', L: '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd'}
FALLBACK={N: 'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f', L: 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc'}
ACCEPT={N: {'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f': 'y2s83-thumbfix5', '15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2 (thumbfix3)', 'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'y2s81-thumbcomp5', '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp', '1e11b1666376f1a701fc90f4db7b815922630cee1a1793915073468ebe512fe7': 'gpufinal1 (y2s90-final)', '5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf': 'thumbgrid1 (y2s91-thumbgrid)', 'a6997fb7630c0b20beef7f7044e43575bf95ecdb87cdfcb16050fb5028518f2d': 'usecount1 (y2s84)', 'b5e8a282738e6c457e3444668f2f3db7ca05df6852f86c91e072e1937b1f4b3f': 'photosperf1 (y2s85)', '4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7': 'gamepipe1 (y2s86)', 'be7a746dd512f3a16023f40f3d507c9e628e47c7f15a18054b3c311fd5f37041': 'gpufinal2 (y2s92-final)', 'e31a9b073095a22d0bd3fe07e6dc72f6075fa669b2377ea76aded1eb97a0f3f0': 'gpufinal3 (y2s93-final)'},
        L: {'3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0': 'y2s75-libmerge4', '1eae024e2382da3086eb942c9e9bfc010e45560afac809feedfaeb972e3f2908': 'y2s75-libmerge5', 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc': 'y2s75-libmerge6', '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd': 'y2s91-thumbgrid library (libmerge6 based)'}}
FLAGS=('no-final', 'usecount-off', 'usecount-keepfreeze', 'no-photosperf', 'no-photosperf-log', 'no-gamepipe', 'no-thumbgrid', 'final-camera', 'usecount-yuv')
MARK=b'made-by-gpufinal4\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-gpufinal4' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        d=open(S+f,'rb').read(); assert hashlib.sha256(d).hexdigest()==FALLBACK[f], 'payload fallback %s is not the expected one'%f; fb[f]=d
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gpufinal4'); did=[]
if cur[N]!=NEW[N]:
    # gpufinal4 is not installed: only stale leftovers (a backup equal to the current file, a record) may be cleaned
    assert cur[N] in ACCEPT[N] and cur[L] in ACCEPT[L], 'the installed Nord2Metal/library is neither gpufinal4 nor a known state: %r'%cur
    for f in (N, L):
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
    if rec: assert rec.get('previous')==cur, 'a record gpufinal4 is there but the files are not gpufinal4 nor its previous ones: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append(f+' stale backup removed')
    if rec:
        rc.pop('gpufinal4'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gpufinal4 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GPUFINAL4_UNDONE', did or 'nothing to do (gpufinal4 not installed)')
else:
    # ---- all checks first ----
    if rec: assert rec.get('installed')==NEW, 'record gpufinal4 does not match: review'
    prev=rec['previous'] if rec else {N: None, L: None}
    src={}
    for f in (N, L):
        p=prev[f]
        if p==NEW[f]:
            assert cur[f]==NEW[f] and not os.path.exists(BAK[f]), 'unexpected state for %s (already new before gpufinal4)'%f
            continue
        if cur[f]!=NEW[f]:
            assert p is None or cur[f]==p, 'the installed %s is neither gpufinal4 nor the one it replaced: %s'%(f, cur[f]); continue
        if os.path.exists(BAK[f]) and (sha(BAK[f])==p or (p is None and sha(BAK[f]) in ACCEPT[f] and sha(BAK[f])!=NEW[f])):
            src[f]=(open(BAK[f],'rb').read(), 'backup')
        elif f in fb and (p==FALLBACK[f] or p is None):
            src[f]=(fb[f], 'fallback in the undo payload')
        else:
            raise AssertionError('no usable copy of the previous %s (%s): backup %s, fallback %s'%(f, p, 'present but different' if os.path.exists(BAK[f]) else 'missing', FALLBACK[f][:12]))
    for f in (N, L):
        if os.path.exists(BAK[f]) and f not in src: assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
    # ---- writes: Nord2Metal, library, receipt, backups ----
    for f in (N, L):
        if f in src:
            put(B+'/'+f, src[f][0], B+'/'+f); inv[f]=sha(B+'/'+f)
            did.append('%s restored to %s %s (%s)'%(f, ACCEPT[f].get(inv[f], '?'), inv[f][:12], src[f][1]))
    rc['inventory'][REL]=inv; rc.pop('gpufinal4', None); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append(f+' backup removed')
    print('GPUFINAL4_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in src}, 'record gpufinal4 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gpufinal4 (y2s94-final Nord2Metal + thumbgrid library) removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GPUFINAL4_FLAG', fl, 'removed')
print('GPUFINAL4_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
