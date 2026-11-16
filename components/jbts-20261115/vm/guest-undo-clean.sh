set +e
# jbts1 UNDO: removes everything jbts1 installed or caused: launchd jobs, /private/var/jb (incl. what apt/Sileo added later), the
# TrollStore-installed app bundles (bundle containers holding the _TrollStore / _TrollStoreLite marker, TrollStore itself included;
# their data containers = user data are kept), SBShowNonDefaultSystemApps back to its pre-install value, the receipt; LaunchServices
# caches moved aside (*.nord2-prev) so the registrations of the removed apps go at the next boot. Every removed path is printed as
# 'JBTS_PATH <path>' (txn_jbts.sh builds the catalog owned list from those lines). No receipt and no prefix = nothing to do.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, hashlib, json, os, plistlib, shutil
R='/work/root'; J=R+'/private/var/jb'; REC='/private/var/db/nord2-jbts.json'
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
SB='/private/var/mobile/Library/Preferences/com.apple.springboard.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
def tree_paths(top):
    out=[top]
    for b,ds,fs in os.walk(top):
        for n in ds+fs: out.append(os.path.join(b,n))
    return out
plan=json.load(open('/src/plan.json')); assert plan['mode']=='undo'
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if rec is None:
    assert not os.path.lexists(J), 'JBTS1_UNDO_REFUSED /private/var/jb exists without the jbts receipt: not ours, not touching it'
raw=rd(LP); rc=json.load(open(R+MR))
assert rc.get('installed_launchd_sha256')==sha(raw), 'JBTS1_UNDO_REFUSED launchd.plist differs from the Metal receipt'
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']; keys=['/System/Library/LaunchDaemons/'+k+'.plist' for k in plan['jobs']]
if rec is None and not any(k in LD for k in keys):
    print('JBTS1_UNDO nothing to undo'); print('JBTS1_UNDONE')
else:
    if any(k in LD for k in keys):
        for k in keys: LD.pop(k,None)
        fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
        put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
        put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
        print('JBTS1_UNDO jobs removed launchd ->', sha(out)[:12])
    if plan.get('remove_trollstore', True):
        for c in sorted(glob.glob(R+'/private/var/containers/Bundle/Application/*')):
            if os.path.isdir(c) and not os.path.islink(c) and (os.path.exists(c+'/_TrollStore') or os.path.exists(c+'/_TrollStoreLite')):
                apps=[n for n in os.listdir(c) if n.endswith('.app')]
                for p in tree_paths(c): print('JBTS_PATH', p[len(R):])
                shutil.rmtree(c); print('JBTS1_UNDO removed TrollStore bundle container', c[len(R):], apps)
    if os.path.lexists(J):
        n=0
        for p in tree_paths(J): print('JBTS_PATH', p[len(R):]); n+=1
        shutil.rmtree(J); print('JBTS1_UNDO removed /private/var/jb', n, 'entries')
    if os.path.lexists(J+'.nord2tmp'): print('JBTS_PATH', '/private/var/jb.nord2tmp'); shutil.rmtree(J+'.nord2tmp')
    prev=(rec or {}).get('springboard_prev_SBShowNonDefaultSystemApps','<absent>')
    if os.path.exists(R+SB):
        sraw=rd(SB); fmt=plistlib.FMT_BINARY if sraw[:6]==b'bplist' else plistlib.FMT_XML; sp=plistlib.loads(sraw); s0=os.stat(R+SB)
        if prev in ('<absent>','<nofile>'): sp.pop('SBShowNonDefaultSystemApps',None)
        else: sp['SBShowNonDefaultSystemApps']=prev
        put(SB,plistlib.dumps(sp,fmt=fmt),s0.st_mode&0o7777,s0.st_uid,s0.st_gid); print('JBTS_PATH', SB); print('JBTS1_UNDO SpringBoard pref back to', prev)
    for f in glob.glob(R+'/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices*.csstore'):
        if os.path.exists(f+'.nord2-prev'): os.remove(f+'.nord2-prev')
        os.rename(f, f+'.nord2-prev'); print('JBTS_PATH', f[len(R):]); print('JBTS_PATH', f[len(R):]+'.nord2-prev')
    if os.path.exists(R+REC): os.remove(R+REC); print('JBTS_PATH', REC)
    print('JBTS1_UNDONE')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'JBTS1_UNDO_OK'
echo 'CC_JOB_ADDED jbts1-undo'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha, jbts receipt removed'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
