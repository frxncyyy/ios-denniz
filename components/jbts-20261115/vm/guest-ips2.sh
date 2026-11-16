set +e
mount -t hfsplus -o ro,force /dev/vda /work/root || exit 1
python3 - <<'INS'
import glob, os, json
R='/work/root'
fs=sorted(glob.glob(R+'/private/var/mobile/Library/Logs/CrashReporter/*DocumentManager*'), key=os.path.getmtime)
t=open(fs[-1],errors='replace').read(); body=t.split('\n',1)[1]
j=json.loads(body)
imgs=j.get('usedImages',[])
print('IPS2 file', os.path.basename(fs[-1]), 'keys', sorted(j.keys())[:40])
for k in ('exceptionReason','asi','ktriageinfo','procPath','parentProc','coalitionName','responsibleProc'):
    if k in j: print('IPS2', k, json.dumps(j[k])[:600])
for f in j.get('lastExceptionBacktrace',[])[:12]:
    im=imgs[f['imageIndex']] if f.get('imageIndex') is not None and f['imageIndex']<len(imgs) else {}
    print('IPS2 LEB', hex(f.get('imageOffset',0)), f.get('symbol'), im.get('name') or im.get('path'), hex(im.get('base',0)))
INS
umount /work/root
echo CC_JOB_ADDED inspect
echo CC_RECEIPT_UPDATED none
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
