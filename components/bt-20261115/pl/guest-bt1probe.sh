set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# bt1 (bt-20261115, piece 0723): Bluetooth user half. /usr/local/libexec/n2btd (ad-hoc signed) replaces the PROGRAM of the
# launchd job com.apple.BlueTool (same label + MachServices com.apple.BlueTool; UserName root, RunAtLoad, no pressured exit);
# Apple's /usr/sbin/BlueTool stays on disk untouched. BT firmware (the user's own vendor files soc3_0_ram_{mcu,bt}_1a_1_hdr.bin)
# -> /usr/share/firmware/nord2-bt/. Gate /private/var/db/nord2-bt/enable (delete = kill switch); the address file
# /private/var/db/nord2-bt/bdaddr is created by n2btd on its first run and kept by the undo.
# launchd.plist edited like guest-gnssd3.sh (Metal receipt installed_launchd_sha256 checked before, updated after).
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
BIN='/usr/local/libexec/n2btd'; FW='/usr/share/firmware/nord2-bt'; DB='/private/var/db/nord2-bt'; GATE=DB+'/enable'; REC='/private/var/db/nord2-bt.json'
KEY='/System/Library/LaunchDaemons/com.apple.BlueTool.plist'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json'))
F={n:open('/src/'+n,'rb').read() for n in plan['files']}
for n,b in F.items(): assert sha(b)==plan['files'][n], '%s is not the planned file'%n
exe=F['n2btd']; assert exe[:4]==b'\xcf\xfa\xed\xfe' and b'com.apple.BlueTool' in exe, 'not the n2btd Mach-O'
for n in ('soc3_0_ram_mcu_1a_1_hdr.bin','soc3_0_ram_bt_1a_1_hdr.bin'): assert F[n][16:20]==b'ALPS', n
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
assert oldh==sha(raw), 'launchd.plist differs from the Metal receipt before the edit: not touching it'
assert KEY in LD and LD[KEY].get('Label')=='com.apple.BlueTool', 'BT1_REFUSED: no com.apple.BlueTool job in launchd.plist'
old_rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {}
orig=old_rec.get('original_job') or LD[KEY]
assert orig.get('ProgramArguments',[None])[0]=='/usr/sbin/BlueTool', 'BT1_REFUSED: the BlueTool job is not Apple\'s and no receipt says what it was'
JOB=dict(orig); JOB.update({'ProgramArguments':[BIN],'UserName':'root','RunAtLoad':True,'EnablePressuredExit':False,'ThrottleInterval':10})
assert JOB['MachServices']=={'com.apple.BlueTool':True}
if os.path.exists(R+BIN): assert sha(rd(BIN))==sha(exe) or old_rec, 'BT1_REFUSED: a different n2btd without our receipt'
put(BIN, exe, 0o755, 0, 0) if not (os.path.exists(R+BIN) and sha(rd(BIN))==sha(exe)) else None
fw_made=old_rec.get('fw_dir_made', not os.path.exists(R+FW))
os.makedirs(R+FW, exist_ok=True); os.chmod(R+FW,0o755); os.chown(R+FW,0,0)
for n in ('soc3_0_ram_mcu_1a_1_hdr.bin','soc3_0_ram_bt_1a_1_hdr.bin'):
    p=FW+'/'+n
    if not (os.path.exists(R+p) and rd(p)==F[n]): put(p, F[n], 0o644, 0, 0)
db_made=old_rec.get('db_made', not os.path.exists(R+DB))
os.makedirs(R+DB, exist_ok=True); os.chmod(R+DB,0o755); os.chown(R+DB,0,0)
print('BT1_FILES', BIN, sha(exe)[:12], FW, sorted(os.listdir(R+FW)))
# bt5 diagnostics: bluetoothd's os_log mirrored to stderr (OS_ACTIVITY_DT_MODE) into /private/var/db/nord2-bt/bluetoothd.log, which n2btd
# filters into the kernel log ("NORD2 BT U: btd: ..."). The original bluetoothd job is kept in the receipt; the undo restores it.
BKEY='/System/Library/LaunchDaemons/com.apple.bluetoothd.plist'; BLOG=DB+'/bluetoothd.log'
assert BKEY in LD and LD[BKEY].get('Label')=='com.apple.bluetoothd', 'BT1_REFUSED: no com.apple.bluetoothd job'
borig=old_rec.get('original_btd_job') or LD[BKEY]
assert borig.get('ProgramArguments')==['/usr/sbin/bluetoothd'] and 'StandardErrorPath' not in borig, 'BT1_REFUSED: the bluetoothd job is not the one this payload knows'
BJOB=json.loads(json.dumps(borig)); BJOB.setdefault('EnvironmentVariables',{})['OS_ACTIVITY_DT_MODE']='YES'
BJOB['StandardErrorPath']=BLOG; BJOB['StandardOutPath']=BLOG
if not os.path.exists(R+BLOG): put(BLOG,b'',0o644,501,501)
os.chown(R+BLOG,501,501); os.chmod(R+BLOG,0o644)
if LD.get(BKEY)!=BJOB:
    LD[BKEY]=BJOB; print('BT1_BTDLOG bluetoothd os_log -> stderr ->', BLOG)
if LD.get(KEY)!=JOB:
    LD[KEY]=JOB; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('BT1_JOB set', json.dumps(JOB['ProgramArguments']), 'launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
elif plistlib.loads(rd(LP))!=cfg:
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('BT1_JOB already; bluetoothd job updated, launchd', oldh[:12], '->', rc['installed_launchd_sha256'][:12])
else: print('BT1_JOB already')
# bt16: A2DP needs Apple's Bluetooth audio HAL plug-in (bluetoothd hands its AudioSkywalkPipe to it: kBTAudioMsgPropertyNexusUuid /
# SkywalkKey). The kernel's audio installer (bsd/dev/audio_install.c custom(): exact entry allowlist) refuses launchd unless
# /System/Library/Audio/Plug-Ins/HAL holds ONLY RedwoodAudio.driver (bt35b: "HAL recovery required error=22"). It does not list
# /Library/Audio/Plug-Ins/HAL (only VirtualAudio.plugin's identity there and its absence from HAL.disabled), which mediaserverd also
# scans (it loaded VirtualAudio.plugin from there). So the copy goes to /Library/Audio/Plug-Ins/HAL/BTAudioHALPlugin.driver;
# HAL.nord2-original is only read. A bt15 copy in /System/.../HAL (ours per receipt) is removed first.
import shutil
SHAL='/System/Library/Audio/Plug-Ins/HAL'; HALO='/System/Library/Audio/Plug-Ins/HAL.nord2-original'; BTH='BTAudioHALPlugin.driver'
LHAL='/Library/Audio/Plug-Ins/HAL'
if old_rec.get('bt_hal_added') and os.path.isdir(R+SHAL+'/'+BTH):
    shutil.rmtree(R+SHAL+'/'+BTH); print('BT1_BTHAL removed the bt15 copy from', SHAL, '(the kernel audio check allows only RedwoodAudio.driver there)')
shal=sorted(os.listdir(R+SHAL)) if os.path.isdir(R+SHAL) else None
assert shal is None or BTH not in shal, 'BT1_REFUSED: %s/%s exists and is not ours'%(SHAL,BTH)
hal_lib=old_rec.get('bt_hal_lib_added', False)
if plan.get('bt_hal', True) and os.path.isdir(R+HALO+'/'+BTH) and os.path.isdir(R+LHAL) and not os.path.exists(R+LHAL+'/'+BTH):
    shutil.copytree(R+HALO+'/'+BTH, R+LHAL+'/'+BTH, symlinks=True)
    for root, dirs, files in os.walk(R+HALO+'/'+BTH):
        for nm in dirs + files + ['.']:
            src=os.path.normpath(os.path.join(root, nm)); dst=src.replace(R+HALO+'/'+BTH, R+LHAL+'/'+BTH, 1)
            st=os.lstat(src); os.lchown(dst, st.st_uid, st.st_gid)
    hal_lib=True; print('BT1_BTHAL', BTH, 'copied into', LHAL)
elif os.path.exists(R+LHAL+'/'+BTH): print('BT1_BTHAL already in', LHAL, '(ours)' if hal_lib else '(not ours: left alone)')
else: print('BT1_BTHAL not available' if plan.get('bt_hal', True) else 'BT1_BTHAL skipped by plan')
print('BT1_AUDIOCHECK', SHAL, 'entries', shal)
put(REC,(json.dumps({'bin':BIN,'sha256':sha(exe),'fw_dir':FW,'fw_dir_made':fw_made,'db':DB,'db_made':db_made,'job':KEY,
                     'original_job':orig,'original_btd_job':borig,'payload':'bt1','launchd_before':old_rec.get('launchd_before',oldh),
                     'bt_hal_added':False,'bt_hal_lib_added':hal_lib},indent=1)+'\n').encode(),0o644,0,0)
if not os.path.exists(R+GATE): put(GATE,b'on\n',0o644,0,0)
STAGE=DB+'/stage'
if plan.get('stage'): put(STAGE,(str(plan['stage'])+'\n').encode(),0o644,0,0); print('BT1_STAGE', plan['stage'], '(kern.n2bt_maxstep: 1 = AP-side reads only, 9 = controller up not shown to iOS)')
elif os.path.exists(R+STAGE): os.remove(R+STAGE); print('BT1_STAGE full (stage file removed)')
for p in (DB+'/guard', DB+'/guard.tmp', GATE+'.off-reset'):          # bt2: a (re)install re-arms the per-boot guard
    if os.path.lexists(R+p): os.remove(R+p); print('BT1_REARM removed', p)
print('BT1_GATE on', GATE)
# bt6: MobileGestalt cache moved aside -> rebuilt on the next boot, after the kernel has put the Bluetooth address into
# IODeviceTree:/chosen (MobileGestalt "bluetooth" = address present; it was cached false). Original kept for the undo.
MG='/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist'
MGB=DB+'/MobileGestalt.plist.pre-bt'
if os.path.exists(R+MG):
    if not os.path.exists(R+MGB): os.replace(R+MG, R+MGB); print('BT1_MGCACHE moved aside (backup', MGB+')')
    else: os.remove(R+MG); print('BT1_MGCACHE moved aside (backup from an earlier install kept)')
else: print('BT1_MGCACHE absent (rebuilds by itself)')
if os.path.exists(R+DB+'/mg-rebuilds'): os.remove(R+DB+'/mg-rebuilds')
print('BT1_OK')
os.sync()
GUEST
sync
umount /work/root
echo 'CC_JOB_ADDED bt1 (BlueTool job program -> n2btd)'
echo 'CC_RECEIPT_UPDATED metal receipt launchd sha (bt1)'
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
