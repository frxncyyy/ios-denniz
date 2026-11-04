set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# undo of gpufinal7 (y2s97-final Nord2Metal + library): each file gpufinal7 replaced is put back exactly as the receipt record 'gpufinal7'
# says (previous), from its backup <file>.pre-gpufinal7 -- on the phone gpufinal6 fe406e9c + d9dd236e (or gpufinal5 / gpufinal4 /
# gpufinal3), whose own records and backups were left, so guest-gpufinal6-undo.sh (then -gpufinal4-undo.sh ...) works afterwards.
# If a backup is missing, the fallback shipped in this payload dir (Nord2Metal = y2s83-thumbfix5 f90a091b, libredwoodair.dylib = libmerge6
# fe59bddb) is used only when it is exactly that previous file (or, with no record at all, as the known-good thumbfix5 + libmerge6 pair);
# gpufinal6/5/4/3, 7fa81fcf, 55de13de and d9dd236e have no fallback (backup required). Backups and record removed; switch files with
# the marker 'made-by-gpufinal7' removed, all others kept.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'c86699eaa8aa35ae9518015b51fbe03e0a585ac5848441a1c094663d888a0659', L: '17c34bcd3ee8a0982b6648f0dcfe9f524432341807d2f5d82e4cb7140707266c'}
FALLBACK={N: 'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f', L: 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc'}
ACCEPT={N: {'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f': 'y2s83-thumbfix5', '15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2 (thumbfix3)', 'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'y2s81-thumbcomp5', '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp', '1e11b1666376f1a701fc90f4db7b815922630cee1a1793915073468ebe512fe7': 'gpufinal1 (y2s90-final)', '5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf': 'thumbgrid1 (y2s91-thumbgrid)', 'a6997fb7630c0b20beef7f7044e43575bf95ecdb87cdfcb16050fb5028518f2d': 'usecount1 (y2s84)', 'b5e8a282738e6c457e3444668f2f3db7ca05df6852f86c91e072e1937b1f4b3f': 'photosperf1 (y2s85)', '4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7': 'gamepipe1 (y2s86)', 'be7a746dd512f3a16023f40f3d507c9e628e47c7f15a18054b3c311fd5f37041': 'gpufinal2 (y2s92-final)', 'e31a9b073095a22d0bd3fe07e6dc72f6075fa669b2377ea76aded1eb97a0f3f0': 'gpufinal3 (y2s93-final)', '015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f': 'gpufinal4 (y2s94-final)', '29abe952646c73ecd31470eea27103576745e99a6d45bf5ace435260b9b84c31': 'gpufinal5 (y2s95-final)', 'fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe': 'gpufinal7 (y2s96-final)'},
        L: {'3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0': 'y2s75-libmerge4', '1eae024e2382da3086eb942c9e9bfc010e45560afac809feedfaeb972e3f2908': 'y2s75-libmerge5', 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc': 'y2s75-libmerge6', '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd': 'y2s91-thumbgrid library (libmerge6 based)', '55de13de7b4b444f40c755dcdd3d240bc7242e5acdd796a933e5582d45969a2e': 'y2s95-final library (gpufinal5)', 'd9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb': 'y2s96-final library (gpufinal7)'}}
FLAGS=('no-final', 'usecount-off', 'usecount-keepfreeze', 'no-photosperf', 'no-photosperf-log', 'no-gamepipe', 'no-thumbgrid', 'final-camera', 'usecount-yuv', 'no-pressure', 'pressure-dryrun', 'pressure-on', 'no-rt1', 'no-singletarget', 'rt1-deadstore', 'trace-cc', 'backdrop-rename', 'no-backdrop')
MARK=b'made-by-gpufinal7\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-gpufinal7' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
fb={}
for f in (N, L):
    if os.path.exists(S+f):
        d=open(S+f,'rb').read(); assert hashlib.sha256(d).hexdigest()==FALLBACK[f], 'payload fallback %s is not the expected one'%f; fb[f]=d
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gpufinal7'); did=[]
if cur[N]!=NEW[N]:
    # gpufinal7 is not installed: only stale leftovers (a backup equal to the current file, a record) may be cleaned
    assert cur[N] in ACCEPT[N] and cur[L] in ACCEPT[L], 'the installed Nord2Metal/library is neither gpufinal7 nor a known state: %r'%cur
    for f in (N, L):
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'unexpected backup '+BAK[f]
    if rec: assert rec.get('previous')==cur, 'a record gpufinal7 is there but the files are not gpufinal7 nor its previous ones: review'
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append(f+' stale backup removed')
    if rec:
        rc.pop('gpufinal7'); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP); did.append('stale record removed')
        print('CC_RECEIPT_UPDATED record gpufinal7 removed')
    else: print('CC_RECEIPT_UPDATED unchanged')
    print('GPUFINAL7_UNDONE', did or 'nothing to do (gpufinal7 not installed)')
else:
    # ---- all checks first ----
    if rec: assert rec.get('installed')==NEW, 'record gpufinal7 does not match: review'
    prev=rec['previous'] if rec else {N: None, L: None}
    src={}
    for f in (N, L):
        p=prev[f]
        if p==NEW[f]:
            assert cur[f]==NEW[f] and not os.path.exists(BAK[f]), 'unexpected state for %s (already new before gpufinal7)'%f
            continue
        if cur[f]!=NEW[f]:
            assert p is None or cur[f]==p, 'the installed %s is neither gpufinal7 nor the one it replaced: %s'%(f, cur[f]); continue
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
    rc['inventory'][REL]=inv; rc.pop('gpufinal7', None); put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    for f in (N, L):
        if os.path.exists(BAK[f]): os.unlink(BAK[f]); did.append(f+' backup removed')
    print('GPUFINAL7_UNDONE', did); print('CC_RECEIPT_UPDATED inventory', {f: inv[f][:12] for f in src}, 'record gpufinal7 removed')
assert inventory(B)==json.load(open(RP))['inventory'][REL]
print('CC_JOB_ADDED gpufinal7 (y2s97-final Nord2Metal + library) removed')
for fl in FLAGS:
    dst=DB+'/'+fl
    if os.path.exists(dst) and open(dst,'rb').read()==MARK: os.unlink(dst); print('GPUFINAL7_FLAG', fl, 'removed')
print('GPUFINAL7_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
