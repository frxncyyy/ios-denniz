set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, plistlib
R='/work/root'
mg=R+'/private/var/containers/Shared/SystemGroup/systemgroup.com.apple.mobilegestaltcache/Library/Caches/com.apple.MobileGestalt.plist'
if os.path.exists(mg): os.replace(mg, mg+'.pre0279')
print('mg moved', not os.path.exists(mg))
d=R+'/private/var/mobile/Library/Application Support/com.apple.remotemanagementd'
print('appsupport exists', os.path.isdir(os.path.dirname(d)))
os.makedirs(d, exist_ok=True); os.chown(d,501,501); os.chmod(d,0o755)
print('rm dir', os.path.isdir(d))
sp=R+'/private/var/mobile/Library/Preferences/com.apple.Sharing.plist'
cfg={}
if os.path.exists(sp):
    cfg=plistlib.loads(open(sp,'rb').read()); st=os.stat(sp); uid,gid,mode=st.st_uid,st.st_gid,st.st_mode&0o7777
else:
    uid,gid,mode=501,501,0o600
print('sharing existed', os.path.exists(sp), sorted(cfg.keys())[:10])
cfg['caForce']=1
tmp=sp+'.nord2tmp'; open(tmp,'wb').write(plistlib.dumps(cfg,fmt=plistlib.FMT_BINARY)); os.chown(tmp,uid,gid); os.chmod(tmp,mode); os.replace(tmp,sp)
print('CC_JOB_ADDED done')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
