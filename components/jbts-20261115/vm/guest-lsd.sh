set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import os, plistlib, glob, json
R='/work/root'
for f in sorted(glob.glob(R+'/System/Library/LaunchDaemons/com.apple.jetsamproperties*')):
    d=plistlib.load(open(f,'rb')); print('JP file', f[len(R):], sorted(d.keys())[:10])
    def walk(o,path):
        if isinstance(o,dict):
            for k,v in o.items():
                if any(x in str(k) for x in ('lsd','installd','pkd','fileproviderd','Default','SpringBoard')) and not isinstance(v,(dict,list)) or str(k) in ('lsd','/usr/libexec/lsd','com.apple.lsd','installd','fileproviderd'):
                    print('JP', path+'/'+str(k), json.dumps(v,default=str)[:300])
                walk(v,path+'/'+str(k))
        elif isinstance(o,list):
            for i,v in enumerate(o): walk(v,path+'[%d]'%i)
    walk(d,'')
I='/private/var/mobile/Documents/ipa-inbox'
if os.path.exists(R+I+'/result.log'): print('RESULTLOG', open(R+I+'/result.log').read()[-3000:].replace('\n',' || '))
print('INBOX', sorted(os.listdir(R+I)) if os.path.isdir(R+I) else None, sorted(os.listdir(R+I+'/done')) if os.path.isdir(R+I+'/done') else None, sorted(os.listdir(R+I+'/failed')) if os.path.isdir(R+I+'/failed') else None)
for d in glob.glob(R+'/private/var/containers/Bundle/Application/*'):
    apps=[n for n in os.listdir(d) if n.endswith('.app')]; mk=[n for n in os.listdir(d) if n.startswith('_TrollStore')]
    if apps: print('BUNDLE', d[len(R):].split('/')[-1], apps, mk)
for f in glob.glob(R+'/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices*'):
    st=os.stat(f); print('LSDB', f[len(R):].split('/Caches/')[1], st.st_size, st.st_mtime)
for f in ('/private/var/jb/var/log/nord2-jb.log','/private/var/jb/var/log/ipainbox-job.log'):
    if os.path.exists(R+f): print('LOG', f, open(R+f,errors='replace').read()[-1500:].replace('\n',' || '))
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
