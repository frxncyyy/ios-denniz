set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os
R='/work/root'; REL='System/Library/Extensions/Nord2Metal.bundle'; B=R+'/'+REL; DB=R+'/private/var/db/nord2-metal'
# gpufinal6 = y2s96-final: y2s95-final (gpufinal4: camera gate + no use-count hold on YCbCr surfaces outside Photos; + the y2s95
# memory-pressure handlers, now OFF unless /var/db/nord2-metal/pressure-on exists) + y2s96 (rt1): (B) MTLLoadAction/StoreActionDontCare
# of colour attachments >= 1 honoured in Panfrost (no preload of a DontCare-loaded attachment, no store of a DontCare-stored one, with
# the batch-merge rules of rwa_pan_fb.c) and (C) per command buffer and target, attachment 1 left out of the framebuffer when no pass
# writes, clears or fetches it. Line 'NORD2 GPU FB: ...' every 5 s per process. Switches: no-rt1 (B off), no-singletarget (C off),
# rt1-deadstore (opt-in extension), pressure-on (y2s95 handlers on). payload-gpufinal6-off ships no-rt1 + no-singletarget (= gpufinal4).
# BOTH files change (Nord2Metal fe406e9c + libredwoodair.dylib d9dd236e), in ONE transaction: every check runs before the first write;
# writes are backups -> library -> Nord2Metal -> receipt (record 'gpufinal6').
# Accepted current Nord2Metal: gpufinal5 (y2s95-final), gpufinal4 (y2s94-final) and gpufinal3 (y2s93-final) -- their records/backups are
# left for their own undo --, gpufinal2, thumbfix5/4/2, thumbcomp5..1, gpufinal1, thumbgrid1, usecount1, photosperf1, gamepipe1; current
# library: libmerge4/5/6, the thumbgrid library 7fa81fcf or the y2s95 library 55de13de. Backups Nord2Metal.pre-gpufinal6 and
# libredwoodair.dylib.pre-gpufinal6 (restored by guest-gpufinal6-undo.sh).
# /var/db/nord2-metal switches: only files with the marker 'made-by-gpufinal6' are made (flag files shipped in the payload dir) or
# cleared (marked, not in this payload); every other switch file is untouched and reported.
N='Nord2Metal'; L='libredwoodair.dylib'; S='/src/'
NEW={N: 'fe406e9c91b42f063235cbac4035977e63bf59012f7c731a0aa4587139435bfe', L: 'd9dd236ef7e7073b7990e75064172d6b2658835ed40e129ce3e72b97d4cfc9eb'}
ACCEPT={N: {'f90a091bb7edd92f00cb5c0f307c07f1ca18b741ca1ff0169195a6c3675d577f': 'y2s83-thumbfix5', '15b8d34b8d7bd6ccf7e42ef679cb93718de273b91b7549a2c3d10b0bd9179843': 'y2s82-thumbfix4', 'a92adbe700c29c18491d90ec0939cce5ab9a70cdf09f2975d0b953b4d4548411': 'y2s76-thumbfix2 (thumbfix3)', 'ac4e92b787d20b185d7744a605faefcc6bf94006e8765f68233a56d74fafd7aa': 'y2s81-thumbcomp5', '4e6591cecd7037f044da980db215b8ec72f3fa4e4f3b51bf53d7b22d99292c01': 'y2s80-thumbcomp4', '2feca08cee00d4b488769405a41b287baae625e0ab867766b43be6a1b151e230': 'y2s79-thumbcomp3', '36c7ad6124afaa4f26204fe631e8acacf1886556fa78ee1851880455e195f8eb': 'y2s78-thumbcomp2', '89708c619d32cce4464bf04684f85352b2d8640ac5ce27671b3ca0d04dafa9dc': 'y2s77-thumbcomp', '1e11b1666376f1a701fc90f4db7b815922630cee1a1793915073468ebe512fe7': 'gpufinal1 (y2s90-final)', '5752a629cb3df4109138a93ad77aef4b132caf8cfcb44d455454c3cfffb2ccbf': 'thumbgrid1 (y2s91-thumbgrid)', 'a6997fb7630c0b20beef7f7044e43575bf95ecdb87cdfcb16050fb5028518f2d': 'usecount1 (y2s84)', 'b5e8a282738e6c457e3444668f2f3db7ca05df6852f86c91e072e1937b1f4b3f': 'photosperf1 (y2s85)', '4e0673355e60e7ba65dba8ca84ae1023da55fdf9cee87d070eed07a0c3dcf3d7': 'gamepipe1 (y2s86)', 'be7a746dd512f3a16023f40f3d507c9e628e47c7f15a18054b3c311fd5f37041': 'gpufinal2 (y2s92-final)', 'e31a9b073095a22d0bd3fe07e6dc72f6075fa669b2377ea76aded1eb97a0f3f0': 'gpufinal3 (y2s93-final)', '015f781d7f9dfa91ef012af8c903c11de0c8a313fd5b9ea895568bd895c5053f': 'gpufinal4 (y2s94-final)', '29abe952646c73ecd31470eea27103576745e99a6d45bf5ace435260b9b84c31': 'gpufinal5 (y2s95-final)'},
        L: {'3d2ed15aec0a0a1a52974f3171249cb670132da58a9b392c79200ef34c602fa0': 'y2s75-libmerge4', '1eae024e2382da3086eb942c9e9bfc010e45560afac809feedfaeb972e3f2908': 'y2s75-libmerge5', 'fe59bddbb8e695284939752823c9ad4b9acdf1d658f1b75767a60428aae87adc': 'y2s75-libmerge6', '7fa81fcfa0daaabed21d4aa9c14dbba6ce2e0579de794f67eb225e6c7a388ebd': 'y2s91-thumbgrid library (libmerge6 based)', '55de13de7b4b444f40c755dcdd3d240bc7242e5acdd796a933e5582d45969a2e': 'y2s95-final library (gpufinal5)'}}
FLAGS=('no-final', 'usecount-off', 'usecount-keepfreeze', 'no-photosperf', 'no-photosperf-log', 'no-gamepipe', 'no-thumbgrid', 'final-camera', 'usecount-yuv', 'no-pressure', 'pressure-dryrun', 'pressure-on', 'no-rt1', 'no-singletarget', 'rt1-deadstore', 'trace-cc')
MARK=b'made-by-gpufinal6\n'
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
RP=DB+'/receipt.json'; BAK={f: DB+'/'+f+'.pre-gpufinal6' for f in (N, L)}
rc=json.load(open(RP)); inv=dict(rc['inventory'][REL])
assert rc.get('version')==2 and rc.get('phase')=='committed', 'unexpected addon receipt'
assert inventory(B)==inv, 'the installed bundle differs from its receipt: %r'%sorted(set(inventory(B).items())^set(inv.items()))
data={f: open(S+f,'rb').read() for f in (N, L)}
for f in (N, L): assert hashlib.sha256(data[f]).hexdigest()==NEW[f], 'payload file %s is not the y2s96-final one'%f
cur={f: sha(B+'/'+f) for f in (N, L)}; rec=rc.get('gpufinal6')
if cur==NEW and rec:
    assert rec.get('installed')==NEW, 'record gpufinal6 does not match: review'
    for f in (N, L):
        prev=rec['previous'][f]
        if prev==NEW[f]: assert not os.path.exists(BAK[f]), 'unexpected backup '+BAK[f]
        else: assert os.path.exists(BAK[f]) and sha(BAK[f])==prev, 'half-installed (backup %s missing or different): review'%BAK[f]
    print('GPUFINAL6_ALREADY'); print('CC_JOB_ADDED gpufinal6 (y2s96-final Nord2Metal + library) already installed'); print('CC_RECEIPT_UPDATED unchanged')
else:
    # ---- all checks first: nothing is written unless the whole pair can be installed ----
    assert not rec, 'a stale receipt record gpufinal6 is there (installed %r): run guest-gpufinal6-undo.sh first'%rec
    assert cur[N]!=NEW[N] and cur[L]!=NEW[L], 'a y2s96-final file is installed without its record (%r): review'%cur
    todo=[]
    for f in (N, L):
        assert inv[f]==cur[f], 'receipt/bundle mismatch for '+f
        assert cur[f] in ACCEPT[f], 'the installed %s is none of the known states: %s'%(f, cur[f])
        if cur[f]==NEW[f]:
            assert not os.path.exists(BAK[f]), 'unexpected backup %s (the %s is already the new one)'%(BAK[f], f)
            continue
        if os.path.exists(BAK[f]): assert sha(BAK[f])==cur[f], 'a different backup %s is already there'%BAK[f]
        todo.append(f)
    for f in todo: assert not os.path.exists(B+'/'+f+'.nord2tmp') and not os.path.exists(BAK[f]+'.nord2tmp'), 'leftover .nord2tmp for '+f
    # ---- writes: backups, library, Nord2Metal, receipt ----
    for f in todo:
        if not os.path.exists(BAK[f]): put(BAK[f], open(B+'/'+f,'rb').read(), B+'/'+f)
        assert sha(BAK[f])==cur[f]
    for f in (L, N):
        if f in todo: put(B+'/'+f, data[f], B+'/'+f); assert sha(B+'/'+f)==NEW[f]; inv[f]=NEW[f]
    rc['inventory'][REL]=inv
    rc['gpufinal6']={'previous': cur, 'previous_name': {f: ACCEPT[f][cur[f]] for f in (N, L)}, 'installed': NEW}
    put(RP, (json.dumps(rc,indent=2)+'\n').encode(), RP)
    assert inventory(B)==json.load(open(RP))['inventory'][REL]
    print('GPUFINAL6_INSTALLED', {f: ACCEPT[f][cur[f]]+' '+cur[f][:12]+' -> '+NEW[f][:12] for f in (N, L)}, 'backups', [os.path.relpath(BAK[f],R) for f in todo] or 'none')
    if L not in todo: print('GPUFINAL6_LIBRARY already the y2s96 library: left as is, no backup')
    print('CC_JOB_ADDED gpufinal6: Nord2Metal + libredwoodair y2s96-final (gpufinal4 + rt1 DontCare + single target; pressure handlers off unless pressure-on)')
    print('CC_RECEIPT_UPDATED inventory', {f: NEW[f][:12] for f in todo}, 'record gpufinal6')
for fl in FLAGS:
    src=S+fl; dst=DB+'/'+fl
    if os.path.exists(src):
        if not os.path.exists(dst):
            with open(dst,'xb') as f: f.write(MARK); f.flush(); os.fsync(f.fileno())
            os.chmod(dst,0o644)
        print('GPUFINAL6_FLAG', fl, 'set' if open(dst,'rb').read()==MARK else 'present (not ours, kept)')
    elif os.path.exists(dst) and open(dst,'rb').read()==MARK:
        os.unlink(dst); print('GPUFINAL6_FLAG', fl, 'cleared (not in this payload)')
for fl in sorted(os.listdir(DB)):
    p=DB+'/'+fl
    if os.path.isfile(p) and (fl in FLAGS or 'thumbgrid' in fl or 'usecount' in fl or 'photosperf' in fl or 'gamepipe' in fl or fl.startswith('photos-ycbcr') or 'pressure' in fl or 'rt1' in fl or 'singletarget' in fl or fl == 'trace-cc') and '.pre-' not in fl:
        body=open(p,'rb').read(64)
        if body!=MARK: print('GPUFINAL6_SWITCH', fl, 'present, kept (%s)'%(body.decode('latin1').strip() or 'empty'))
if os.path.exists(DB+'/no-final') and open(DB+'/no-final','rb').read()!=MARK:
    print('GPUFINAL6_NOTE no-final is present and not ours: the final (y2s94) features stay OFF (master switch) until it is removed')
for fl, what in (('no-rt1', 'the rt1 DontCare handling (B) stays OFF'), ('no-singletarget', 'the single-target framebuffer (C) stays OFF'),
                 ('pressure-on', 'the y2s95 pressure handlers are ON'), ('rt1-deadstore', 'the deadstore extension is ON')):
    if os.path.exists(DB+'/'+fl) and open(DB+'/'+fl,'rb').read()!=MARK:
        print('GPUFINAL6_NOTE', fl, 'is present and not ours:', what, 'until it is removed')
print('GPUFINAL6_BUNDLE', {f: sha(B+'/'+f)[:12] for f in (N, L)}, sorted(os.listdir(B)))
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
