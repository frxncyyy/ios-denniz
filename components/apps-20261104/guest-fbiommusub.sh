set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# 0409 guest-fbiommusub.sh = guest-fbiommu.sh + token sub (n2fbhwctl fork iommusub-20261113/fbiommuctl, kern.n2fbiommu_sub).
# 0401 guest-fbiommu.sh = guest-fbhwctl.sh + the config tokens iommu/probe/iotest/hwtest of the 0401 n2fbhwctl fork
# (gpu/build/iommu-20261112/fbiommuctl; same binary path, job, config file and receipt, so guest-fbhwctl-undo.sh removes it).
# fbhwctl: display HW composition mode control (DISPLAY_HW_COMPOSE.md "Build/test"). /usr/local/libexec/n2fbhwctl
# (gpu/build/hwcomp-20261112/fbhwctl, freestanding, ad-hoc signed) + launchd job com.nord2.fbhwctl (RunAtLoad, root, one run per boot)
# + config /private/var/db/nord2-fbhw-mode (one line from plan.json 'config', also the gate). The job sets the sysctls kern.n2fbhw
# (0396) / kern.n2fbhw_pool (0394) on the schedule of the config and logs "N2FBHWCTL ..." lines to the console.
# Same guest for every payload (payload-fbhwmode0..3 = fixed mode, payload-fbhwcycle = 0,1,2,3 x 120 s); a second install over the
# first only rewrites the config (and the binary if it changed). Undo: guest-fbhwctl-undo.sh.
# launchd.plist edited like guest-gnssd1.sh (Metal receipt installed_launchd_sha256 checked before, updated after).
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2fbhwctl'; CFG='/private/var/db/nord2-fbhw-mode'; REC='/private/var/db/nord2-fbhwctl.json'
LABEL='com.nord2.fbhwctl'; KEY='/System/Library/LaunchDaemons/'+LABEL+'.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json'))
exe=open('/src/n2fbhwctl','rb').read(); assert sha(exe)==plan['files']['n2fbhwctl'], 'n2fbhwctl is not the planned file'
assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'N2FBHWCTL phase %d/%d' in exe, 'not the n2fbhwctl Mach-O'
cfgline=plan['config'].strip()+'\n'; toks=cfgline.split()
assert toks and all(t.split('=')[0] in ('modes','period','delay','pool','rounds','final','iommu','probe','iotest','hwtest','stages','stagegap','sub','subgap','subdelay') for t in toks) and any(t.startswith('modes=') for t in toks), 'bad config'
JOB={'Label':LABEL,'UserName':'root','RunAtLoad':True,'ProcessType':'Interactive','EnablePressuredExit':False,
     'ProgramArguments':[BIN,'--config',CFG]}
# ---- checks first
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
assert not any(isinstance(j,dict) and j.get('Label')==LABEL for k,j in LD.items() if k!=KEY), 'label used elsewhere'
assert os.path.isdir(R+'/usr/local/libexec')
if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or os.path.exists(R+REC), 'a different n2fbhwctl without our receipt: review'
if os.path.exists(R+CFG): assert os.path.exists(R+REC), CFG+' exists without our receipt: review'
old_rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
# ---- binary + config
if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)): put(BIN, exe, 0o755, 0, 0)
put(CFG, cfgline.encode(), 0o644, 0, 0)
print('FBHWCTL_FILES', BIN, sha(exe)[:12], CFG, repr(cfgline.strip()))
# ---- job
if LD.get(KEY)!=JOB:
    LD[KEY]=JOB; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('FBHWCTL_JOB set', json.dumps(JOB['ProgramArguments']), 'launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
else: print('FBHWCTL_JOB already')
put(REC,(json.dumps({'bin':BIN,'sha256':sha(exe),'config':cfgline.strip(),'job':KEY,'payload':plan.get('name'),
                     'launchd_before':old_rec.get('launchd_before',oldh)},indent=1)+'\n').encode(),0o644,0,0)
print('CC_JOB_ADDED', LABEL); print('CC_RECEIPT_UPDATED metal receipt launchd sha')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
