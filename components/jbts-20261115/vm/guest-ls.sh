set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, plistlib, glob, json, struct
R='/work/root'
A='/System/Library/Frameworks/FileProvider.framework/PlugIns/LocalStorageFileProvider.appex'
ip=plistlib.load(open(R+A+'/Info.plist','rb'))
print('LSINFO', json.dumps(ip.get('NSExtension'), default=str)[:1500])
print('LSINFO keys', sorted(ip.keys()))
exe=R+A+'/'+ip['CFBundleExecutable']; b=open(exe,'rb').read()
i=b.find(b'<?xml'); j=b.find(b'</plist>', i)
if i>=0: print('LSENTS', b[i:j+8].decode(errors='replace').replace('\n','')[:2500])
P='/Applications/MobileSlideShow.app/PlugIns/PhotosFileProvider.appex'
ip2=plistlib.load(open(R+P+'/Info.plist','rb')); print('PHINFO', json.dumps(ip2.get('NSExtension'), default=str)[:600])
for m in glob.glob(R+'/private/var/mobile/Containers/Shared/AppGroup/*/.com.apple.mobile_container_manager.metadata.plist'):
    try: d=plistlib.load(open(m,'rb'))
    except Exception: continue
    ident=d.get('MCMMetadataIdentifier','')
    if 'FileProvider' in ident or 'DocumentsApp' in ident or 'LocalStorage' in ident:
        c=os.path.dirname(m); print('GROUP', ident, c[len(R):], sorted(os.listdir(c))[:20], json.dumps({k:d[k] for k in d if k!='MCMMetadataInfo'},default=str)[:300])
fp='/private/var/mobile/Library/Application Support/FileProvider'
for f in glob.glob(R+fp+'/**', recursive=True)[:40]: print('FPSTATE', f[len(R):], os.path.getsize(f) if os.path.isfile(f) else 'dir')
pl=R+fp+'/com.apple.mobileslideshow.PhotosFileProvider/Domains.plist'
if os.path.exists(pl): print('PHDOMAINS', json.dumps(plistlib.load(open(pl,'rb')),default=str)[:500])
for p in ('/private/var/mobile/Library/Preferences/com.apple.DocumentsApp.plist','/private/var/mobile/Library/Preferences/com.apple.FileProvider.plist','/private/var/mobile/Library/Preferences/com.apple.fileproviderd.plist'):
    if os.path.exists(R+p): print('PREF', p, json.dumps(plistlib.load(open(R+p,'rb')),default=str)[:600])
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
