set +e
# fplog1 ('On My iPhone' missing): diagnostics + one harmless state fix, rootfs only. plan.mode = 'install' or 'undo'.
#  A) logd subsystem configs in /Library/Preferences/Logging/Subsystems for com.apple.FileProvider, com.apple.DocumentManager,
#     com.apple.pluginkit: persist Info level and Default-Privacy-Setting Public, so the next snapshot's unified log shows
#     fileproviderd's INFO lines ("Not instantiating file providers until setup has been run", domain import, storage URLs) and the
#     arguments now printed as <private>. An existing file of ours is replaced; a foreign file is refused (left alone).
#  B) the LocalStorage provider's document storage directory: group.com.apple.FileProvider.LocalStorage's container has no
#     'File Provider Storage' (Photos' provider group has one; [V mirror 14:41]); created 501:501 0755 if absent, like Photos'.
# Every written/removed path is printed as 'JBTS_PATH <path>' (txn_fplog.sh builds the owned list from them).
# Receipt /private/var/db/nord2-fplog1.json; undo removes the three configs and the directory only if it is still empty.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, json, os, plistlib
R='/work/root'; D='/Library/Preferences/Logging/Subsystems'; REC='/private/var/db/nord2-fplog1.json'
SUBS=('com.apple.FileProvider','com.apple.DocumentManager','com.apple.pluginkit')
CFG={'DEFAULT-OPTIONS': {'Level': {'Enable': 'Info', 'Persist': 'Info'}, 'Default-Privacy-Setting': 'Public'}, 'nord2': 'fplog1'}
def say(p):
    print('JBTS_PATH', p)
    rp=os.path.realpath(R+p)[len(R):]
    if rp!=p: print('JBTS_PATH', rp)       # /Library/Preferences -> /private/var/preferences
plan=json.load(open('/src/plan.json')); undo=plan['mode']=='undo'
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {'configs': [], 'dir_made': None}
# the LocalStorage group container (MCM metadata)
grp=None
for m in glob.glob(R+'/private/var/mobile/Containers/Shared/AppGroup/*/.com.apple.mobile_container_manager.metadata.plist'):
    try: d=plistlib.load(open(m,'rb'))
    except Exception: continue
    if d.get('MCMMetadataIdentifier')=='group.com.apple.FileProvider.LocalStorage': grp=os.path.dirname(m)[len(R):]
print('FPLOG1_GROUP', grp)
if not undo:
    assert os.path.isdir(R+D), 'FPLOG1_REFUSED no '+D
    for s in SUBS:
        p=D+'/'+s+'.plist'
        if os.path.exists(R+p):
            try: cur=plistlib.load(open(R+p,'rb'))
            except Exception: cur={}
            assert cur.get('nord2')=='fplog1', 'FPLOG1_REFUSED %s exists and is not ours'%p
    for s in SUBS:
        p=D+'/'+s+'.plist'; b=plistlib.dumps(CFG, fmt=plistlib.FMT_XML)
        if not (os.path.exists(R+p) and open(R+p,'rb').read()==b):
            with open(R+p,'wb') as f: f.write(b)
            os.chown(R+p,0,0); os.chmod(R+p,0o644); say(p)
        if p not in rec['configs']: rec['configs'].append(p)
    if grp:
        sd=grp+'/File Provider Storage'
        if not os.path.exists(R+sd):
            os.mkdir(R+sd); os.chown(R+sd,501,501); os.chmod(R+sd,0o755); say(sd); rec['dir_made']=sd
        print('FPLOG1_STORAGE', sd, 'made' if rec.get('dir_made')==sd else 'present')
    b=(json.dumps(rec,indent=1)+'\n').encode()
    if not (os.path.exists(R+REC) and open(R+REC,'rb').read()==b):
        with open(R+REC,'wb') as f: f.write(b)
        os.chmod(R+REC,0o644); say(REC)
    print('FPLOG1_INSTALLED', rec)
else:
    for p in rec['configs']:
        if os.path.exists(R+p): os.remove(R+p); say(p)
    sd=rec.get('dir_made')
    if sd and os.path.isdir(R+sd) and not os.listdir(R+sd): os.rmdir(R+sd); say(sd)
    if os.path.exists(R+REC): os.remove(R+REC); say(REC)
    print('FPLOG1_UNDONE')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'FPLOG1_OK'
echo 'CC_JOB_ADDED fplog1 (logging configs + LocalStorage storage dir)'
echo 'CC_RECEIPT_UPDATED /private/var/db/nord2-fplog1.json'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
set +e
# fplog1 ('On My iPhone' missing): diagnostics + one harmless state fix, rootfs only. plan.mode = 'install' or 'undo'.
#  A) logd subsystem configs in /Library/Preferences/Logging/Subsystems for com.apple.FileProvider, com.apple.DocumentManager,
#     com.apple.pluginkit: persist Info level and Default-Privacy-Setting Public, so the next snapshot's unified log shows
#     fileproviderd's INFO lines ("Not instantiating file providers until setup has been run", domain import, storage URLs) and the
#     arguments now printed as <private>. An existing file of ours is replaced; a foreign file is refused (left alone).
#  B) the LocalStorage provider's document storage directory: group.com.apple.FileProvider.LocalStorage's container has no
#     'File Provider Storage' (Photos' provider group has one; [V mirror 14:41]); created 501:501 0755 if absent, like Photos'.
# Every written/removed path is printed as 'JBTS_PATH <path>' (txn_fplog.sh builds the owned list from them).
# Receipt /private/var/db/nord2-fplog1.json; undo removes the three configs and the directory only if it is still empty.
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import glob, json, os, plistlib
R='/work/root'; D='/Library/Preferences/Logging/Subsystems'; REC='/private/var/db/nord2-fplog1.json'
SUBS=('com.apple.FileProvider','com.apple.DocumentManager','com.apple.pluginkit')
CFG={'DEFAULT-OPTIONS': {'Level': {'Enable': 'Info', 'Persist': 'Info'}, 'Default-Privacy-Setting': 'Public'}, 'nord2': 'fplog1'}
def say(p):
    print('JBTS_PATH', p)
    rp=os.path.realpath(R+p)[len(R):]
    if rp!=p: print('JBTS_PATH', rp)       # /Library/Preferences -> /private/var/preferences
plan=json.load(open('/src/plan.json')); undo=plan['mode']=='undo'
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else {'configs': [], 'dir_made': None}
# the LocalStorage group container (MCM metadata)
grp=None
for m in glob.glob(R+'/private/var/mobile/Containers/Shared/AppGroup/*/.com.apple.mobile_container_manager.metadata.plist'):
    try: d=plistlib.load(open(m,'rb'))
    except Exception: continue
    if d.get('MCMMetadataIdentifier')=='group.com.apple.FileProvider.LocalStorage': grp=os.path.dirname(m)[len(R):]
print('FPLOG1_GROUP', grp)
if not undo:
    assert os.path.isdir(R+D), 'FPLOG1_REFUSED no '+D
    for s in SUBS:
        p=D+'/'+s+'.plist'
        if os.path.exists(R+p):
            try: cur=plistlib.load(open(R+p,'rb'))
            except Exception: cur={}
            assert cur.get('nord2')=='fplog1', 'FPLOG1_REFUSED %s exists and is not ours'%p
    for s in SUBS:
        p=D+'/'+s+'.plist'; b=plistlib.dumps(CFG, fmt=plistlib.FMT_XML)
        if not (os.path.exists(R+p) and open(R+p,'rb').read()==b):
            with open(R+p,'wb') as f: f.write(b)
            os.chown(R+p,0,0); os.chmod(R+p,0o644); say(p)
        if p not in rec['configs']: rec['configs'].append(p)
    if grp:
        sd=grp+'/File Provider Storage'
        if not os.path.exists(R+sd):
            os.mkdir(R+sd); os.chown(R+sd,501,501); os.chmod(R+sd,0o755); say(sd); rec['dir_made']=sd
        print('FPLOG1_STORAGE', sd, 'made' if rec.get('dir_made')==sd else 'present')
    b=(json.dumps(rec,indent=1)+'\n').encode()
    if not (os.path.exists(R+REC) and open(R+REC,'rb').read()==b):
        with open(R+REC,'wb') as f: f.write(b)
        os.chmod(R+REC,0o644); say(REC)
    print('FPLOG1_INSTALLED', rec)
else:
    for p in rec['configs']:
        if os.path.exists(R+p): os.remove(R+p); say(p)
    sd=rec.get('dir_made')
    if sd and os.path.isdir(R+sd) and not os.listdir(R+sd): os.rmdir(R+sd); say(sd)
    if os.path.exists(R+REC): os.remove(R+REC); say(REC)
    print('FPLOG1_UNDONE')
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'FPLOG1_OK'
echo 'CC_JOB_ADDED fplog1 (logging configs + LocalStorage storage dir)'
echo 'CC_RECEIPT_UPDATED /private/var/db/nord2-fplog1.json'
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
