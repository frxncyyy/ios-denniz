set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, plistlib, glob, json
R='/work/root'
hits=[]
for top in ('/System/Library','/Applications','/usr/libexec','/private/var/containers/Bundle'):
    for b,ds,fs in os.walk(R+top):
        if b.endswith('.appex') and 'Info.plist' in fs:
            try: ip=plistlib.load(open(b+'/Info.plist','rb'))
            except Exception as e: continue
            ext=ip.get('NSExtension',{}) or {}; ep=ext.get('NSExtensionPointIdentifier') or ip.get('EXAppExtensionAttributes',{}).get('EXExtensionPointIdentifier')
            if ep and ('fileprovider' in ep.lower() or 'FileProvider' in b or 'LocalStorage' in b or 'Storage' in ip.get('CFBundleIdentifier','')):
                attrs=ext.get('NSExtensionAttributes',{})
                print('FPX', b[len(R):], ip.get('CFBundleIdentifier'), ep, json.dumps({k:attrs[k] for k in list(attrs)[:8]}, default=str)[:300])
for p in ('/private/var/mobile/Library/Application Support/FileProvider','/private/var/mobile/Library/Application Support/CloudDocs','/private/var/mobile/Containers/Shared/AppGroup'):
    print('FPDIR', p, os.path.isdir(R+p), (sorted(os.listdir(R+p))[:15] if os.path.isdir(R+p) else ''))
for f in glob.glob(R+'/private/var/mobile/Library/Application Support/FileProvider/**', recursive=True)[:60]: print('FPFILE', f[len(R):], os.path.getsize(f) if os.path.isfile(f) else 'dir')
for f in glob.glob(R+'/private/var/db/*plugin*')+glob.glob(R+'/private/var/mobile/Library/MobileContainerManager/*')[:5]: print('PK', f[len(R):])
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
