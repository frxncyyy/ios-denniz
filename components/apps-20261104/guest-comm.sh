set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import plistlib,json,glob,os
d=plistlib.loads(open('/work/root/System/Library/xpc/launchd.plist','rb').read())['LaunchDaemons']
print('jobs in cache',len(d))
for k,v in d.items():
    s=json.dumps(v,default=str)
    if 'CommCenter' in k or 'commcenter' in s.lower() or 'CoreTelephony' in k or 'telephony' in k.lower():
        print('JOB',k,s[:700])
files=sorted(os.listdir('/work/root/System/Library/LaunchDaemons'))
print('plist files',len(files)); print([f for f in files if 'ommcenter' in f.lower() or 'telephon' in f.lower() or 'baseband' in f.lower() or 'ct' == f[:2].lower()])
PY
umount /work/root
echo INSPECT_DONE
