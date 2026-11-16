set +e
# ipainbox1 (gpu/build/jbts-20261115, made by make_ipainbox.py): needs jbts1 installed (receipt /private/var/db/nord2-jbts.json).
#  block A (only if plan.apps_undo): removes the apps the old apps payload put in /Applications (Terraria.app, SubwaySurf.app) so
#          TrollStore can install the same bundle ids (rc 179 otherwise). Each one is removed only if its Info.plist id AND main
#          executable sha256 equal the original IPA's (apps-20261104/payload); data containers (saves) are kept. LS caches aside.
#  block B: /var/jb/usr/libexec/nord2-jb/ipainbox.sh + launchd job com.nord2.jb.ipainbox (RunAtLoad + WatchPaths) + the inbox
#          /private/var/mobile/Documents/ipa-inbox (mobile:mobile) + the plan's IPAs copied into it (sha256 checked).
# Every path written/removed is printed as 'JBTS_PATH <path>' (txn_ipainbox.sh turns them into the catalog owned list).
# Receipt /private/var/db/nord2-ipainbox.json. Idempotent (job/script equal -> left; IPA already in inbox/done -> skipped).
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, hashlib, json, os, plistlib, shutil
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
JREC='/private/var/db/nord2-jbts.json'; REC='/private/var/db/nord2-ipainbox.json'
I='/private/var/mobile/Documents/ipa-inbox'; SCR='/private/var/jb/usr/libexec/nord2-jb/ipainbox.sh'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def fsha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
def say(p): print('JBTS_PATH', p)
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path); say(path)
plan=json.load(open('/src/plan.json')); assert plan['mode']=='ipainbox'
assert os.path.exists(R+JREC) and os.path.isdir(R+'/private/var/jb/usr/libexec/nord2-jb'), 'IPAINBOX1_REFUSED jbts1 is not installed'
raw=rd(LP); rc=json.load(open(R+MR))
assert rc.get('installed_launchd_sha256')==sha(raw), 'IPAINBOX1_REFUSED launchd.plist differs from the Metal receipt'
script=open('/src/ipainbox.sh','rb').read(); assert sha(script)==plan['script_sha256'], 'IPAINBOX1_REFUSED script is not the planned one'
for e in plan['ipas']: assert fsha('/src/ipas/'+e['name'])==e['sha256'], 'IPAINBOX1_REFUSED %s is not the planned IPA'%e['name']
# ---- block A checks (all before any write) ----
rm=[]
for a in plan.get('apps_undo',[]):
    d='/Applications/'+a['bundle']
    if not os.path.exists(R+d): print('IPAINBOX1_APPSUNDO', a['bundle'], 'already absent'); continue
    ip=plistlib.load(open(R+d+'/Info.plist','rb'))
    assert ip.get('CFBundleIdentifier')==a['bundle_id'], 'IPAINBOX1_REFUSED %s has id %r, not %s'%(d,ip.get('CFBundleIdentifier'),a['bundle_id'])
    exe=R+d+'/'+ip['CFBundleExecutable']
    ok=a['exe_sha256'] if isinstance(a['exe_sha256'],list) else [a['exe_sha256']]
    assert fsha(exe) in ok, 'IPAINBOX1_REFUSED %s main executable differs from the apps payload IPA: not ours, not touching it'%d
    rm.append(d)
sv=os.statvfs(R); need=sum(os.path.getsize('/src/ipas/'+e['name']) for e in plan['ipas'])+(256<<20)
assert sv.f_bavail*sv.f_frsize>need, 'IPAINBOX1_REFUSED not enough free space'
print('IPAINBOX1_CHECKS_OK remove', rm, 'ipas', [e['name'] for e in plan['ipas']])
# ---- block A ----
for d in rm:
    for b,ds,fs in os.walk(R+d):
        for n in ds+fs: say(os.path.join(b,n)[len(R):])
    say(d); shutil.rmtree(R+d); print('IPAINBOX1_APPSUNDO removed', d)
if rm:
    for f in glob.glob(R+'/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices*.csstore'):
        if os.path.exists(f+'.nord2-prev'): os.remove(f+'.nord2-prev')
        os.rename(f,f+'.nord2-prev'); say(f[len(R):]); say(f[len(R):]+'.nord2-prev')
    print('IPAINBOX1_APPSUNDO LaunchServices caches moved aside (rescan at boot)')
# ---- block B ----
if not (os.path.exists(R+SCR) and rd(SCR)==script): put(SCR,script,0o755,0,0)
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']; key='/System/Library/LaunchDaemons/com.nord2.jb.ipainbox.plist'; job=plan['job']
assert not any(isinstance(j,dict) and j.get('Label')==job['Label'] for k,j in LD.items() if k!=key), 'IPAINBOX1_REFUSED label used elsewhere'
if LD.get(key)!=job:
    LD[key]=job; fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('IPAINBOX1_JOB set launchd ->', sha(out)[:12])
else: print('IPAINBOX1_JOB already')
for d in ('/private/var/mobile/Documents', I, I+'/done', I+'/failed'):
    if not os.path.isdir(R+d): os.mkdir(R+d); say(d)
    os.chown(R+d,501,501); os.chmod(R+d,0o755)
for e in plan['ipas']:
    dst=I+'/'+e['name']
    if os.path.exists(R+I+'/done/'+e['name']) and fsha(R+I+'/done/'+e['name'])==e['sha256']: print('IPAINBOX1_IPA', e['name'], 'already installed (done/)'); continue
    if os.path.exists(R+dst) and fsha(R+dst)==e['sha256']: print('IPAINBOX1_IPA', e['name'], 'already queued'); continue
    tmp=R+dst+'.part'
    shutil.copyfile('/src/ipas/'+e['name'], tmp); os.chown(tmp,501,501); os.chmod(tmp,0o644); os.rename(tmp,R+dst); say(dst)
    assert fsha(R+dst)==e['sha256']; print('IPAINBOX1_IPA', e['name'], 'queued', os.path.getsize(R+dst), 'B')
old=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
rec={'payload':'ipainbox1','job':key,'script':SCR,'inbox':I,'apps_removed':sorted(set(old.get('apps_removed',[])+rm)),
     'queued':sorted(set(old.get('queued',[])+[e['name'] for e in plan['ipas']]))}
b=(json.dumps(rec,indent=1)+'\n').encode()
if not (os.path.exists(R+REC) and rd(REC)==b): put(REC,b,0o644,0,0)
print('IPAINBOX1_DONE')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'IPAINBOX1_OK'
echo 'CC_JOB_ADDED ipainbox1 (com.nord2.jb.ipainbox + inbox IPAs)'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha + /private/var/db/nord2-ipainbox.json'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
set +e
# ipainbox1 (gpu/build/jbts-20261115, made by make_ipainbox.py): needs jbts1 installed (receipt /private/var/db/nord2-jbts.json).
#  block A (only if plan.apps_undo): removes the apps the old apps payload put in /Applications (Terraria.app, SubwaySurf.app) so
#          TrollStore can install the same bundle ids (rc 179 otherwise). Each one is removed only if its Info.plist id AND main
#          executable sha256 equal the original IPA's (apps-20261104/payload); data containers (saves) are kept. LS caches aside.
#  block B: /var/jb/usr/libexec/nord2-jb/ipainbox.sh + launchd job com.nord2.jb.ipainbox (RunAtLoad + WatchPaths) + the inbox
#          /private/var/mobile/Documents/ipa-inbox (mobile:mobile) + the plan's IPAs copied into it (sha256 checked).
# Every path written/removed is printed as 'JBTS_PATH <path>' (txn_ipainbox.sh turns them into the catalog owned list).
# Receipt /private/var/db/nord2-ipainbox.json. Idempotent (job/script equal -> left; IPA already in inbox/done -> skipped).
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, hashlib, json, os, plistlib, shutil
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
JREC='/private/var/db/nord2-jbts.json'; REC='/private/var/db/nord2-ipainbox.json'
I='/private/var/mobile/Documents/ipa-inbox'; SCR='/private/var/jb/usr/libexec/nord2-jb/ipainbox.sh'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def fsha(p):
    h=hashlib.sha256()
    with open(p,'rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
def say(p): print('JBTS_PATH', p)
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path); say(path)
plan=json.load(open('/src/plan.json')); assert plan['mode']=='ipainbox'
assert os.path.exists(R+JREC) and os.path.isdir(R+'/private/var/jb/usr/libexec/nord2-jb'), 'IPAINBOX1_REFUSED jbts1 is not installed'
raw=rd(LP); rc=json.load(open(R+MR))
assert rc.get('installed_launchd_sha256')==sha(raw), 'IPAINBOX1_REFUSED launchd.plist differs from the Metal receipt'
script=open('/src/ipainbox.sh','rb').read(); assert sha(script)==plan['script_sha256'], 'IPAINBOX1_REFUSED script is not the planned one'
for e in plan['ipas']: assert fsha('/src/ipas/'+e['name'])==e['sha256'], 'IPAINBOX1_REFUSED %s is not the planned IPA'%e['name']
# ---- block A checks (all before any write) ----
rm=[]
for a in plan.get('apps_undo',[]):
    d='/Applications/'+a['bundle']
    if not os.path.exists(R+d): print('IPAINBOX1_APPSUNDO', a['bundle'], 'already absent'); continue
    ip=plistlib.load(open(R+d+'/Info.plist','rb'))
    assert ip.get('CFBundleIdentifier')==a['bundle_id'], 'IPAINBOX1_REFUSED %s has id %r, not %s'%(d,ip.get('CFBundleIdentifier'),a['bundle_id'])
    exe=R+d+'/'+ip['CFBundleExecutable']
    ok=a['exe_sha256'] if isinstance(a['exe_sha256'],list) else [a['exe_sha256']]
    assert fsha(exe) in ok, 'IPAINBOX1_REFUSED %s main executable differs from the apps payload IPA: not ours, not touching it'%d
    rm.append(d)
sv=os.statvfs(R); need=sum(os.path.getsize('/src/ipas/'+e['name']) for e in plan['ipas'])+(256<<20)
assert sv.f_bavail*sv.f_frsize>need, 'IPAINBOX1_REFUSED not enough free space'
print('IPAINBOX1_CHECKS_OK remove', rm, 'ipas', [e['name'] for e in plan['ipas']])
# ---- block A ----
for d in rm:
    for b,ds,fs in os.walk(R+d):
        for n in ds+fs: say(os.path.join(b,n)[len(R):])
    say(d); shutil.rmtree(R+d); print('IPAINBOX1_APPSUNDO removed', d)
if rm:
    for f in glob.glob(R+'/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices*.csstore'):
        if os.path.exists(f+'.nord2-prev'): os.remove(f+'.nord2-prev')
        os.rename(f,f+'.nord2-prev'); say(f[len(R):]); say(f[len(R):]+'.nord2-prev')
    print('IPAINBOX1_APPSUNDO LaunchServices caches moved aside (rescan at boot)')
# ---- block B ----
if not (os.path.exists(R+SCR) and rd(SCR)==script): put(SCR,script,0o755,0,0)
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']; key='/System/Library/LaunchDaemons/com.nord2.jb.ipainbox.plist'; job=plan['job']
assert not any(isinstance(j,dict) and j.get('Label')==job['Label'] for k,j in LD.items() if k!=key), 'IPAINBOX1_REFUSED label used elsewhere'
if LD.get(key)!=job:
    LD[key]=job; fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); print('IPAINBOX1_JOB set launchd ->', sha(out)[:12])
else: print('IPAINBOX1_JOB already')
for d in ('/private/var/mobile/Documents', I, I+'/done', I+'/failed'):
    if not os.path.isdir(R+d): os.mkdir(R+d); say(d)
    os.chown(R+d,501,501); os.chmod(R+d,0o755)
for e in plan['ipas']:
    dst=I+'/'+e['name']
    if os.path.exists(R+I+'/done/'+e['name']) and fsha(R+I+'/done/'+e['name'])==e['sha256']: print('IPAINBOX1_IPA', e['name'], 'already installed (done/)'); continue
    if os.path.exists(R+dst) and fsha(R+dst)==e['sha256']: print('IPAINBOX1_IPA', e['name'], 'already queued'); continue
    tmp=R+dst+'.part'
    shutil.copyfile('/src/ipas/'+e['name'], tmp); os.chown(tmp,501,501); os.chmod(tmp,0o644); os.rename(tmp,R+dst); say(dst)
    assert fsha(R+dst)==e['sha256']; print('IPAINBOX1_IPA', e['name'], 'queued', os.path.getsize(R+dst), 'B')
old=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
rec={'payload':'ipainbox1','job':key,'script':SCR,'inbox':I,'apps_removed':sorted(set(old.get('apps_removed',[])+rm)),
     'queued':sorted(set(old.get('queued',[])+[e['name'] for e in plan['ipas']]))}
b=(json.dumps(rec,indent=1)+'\n').encode()
if not (os.path.exists(R+REC) and rd(REC)==b): put(REC,b,0o644,0,0)
print('IPAINBOX1_DONE')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'IPAINBOX1_OK'
echo 'CC_JOB_ADDED ipainbox1 (com.nord2.jb.ipainbox + inbox IPAs)'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha + /private/var/db/nord2-ipainbox.json'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
