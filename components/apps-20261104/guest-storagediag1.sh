set +e
# payload-storagediag1: READ-ONLY (mounted ro). Prints SDIAG lines: free space and the 6 newest Preferences reports.
mount -t hfsplus -o ro /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, json, glob
R='/work/root'
st=os.statvfs(R); print('SDIAG_FREE bytes_avail=%d bytes_free=%d'%(st.f_bavail*st.f_frsize, st.f_bfree*st.f_frsize))
fl=sorted(glob.glob(R+'/private/var/mobile/Library/Logs/CrashReporter/Preferences*.ips'), key=os.path.getmtime)[-6:]
for f in fl:
    try:
        h,_,b=open(f,errors='replace').read().partition('\n'); j=json.loads(b); hd=json.loads(h)
    except Exception as e:
        print('SDIAG_REPORT %s unparsable %s'%(os.path.basename(f),e)); continue
    imgs=j.get('usedImages',[]); term=j.get('termination',{}); ex=j.get('exception',{})
    print('SDIAG_REPORT %s bug_type=%s launch=%s capture=%s exc=%s %s term=%s'%(os.path.basename(f),hd.get('bug_type'),j.get('procLaunch'),j.get('captureTime'),ex.get('type'),ex.get('signal'),json.dumps(term.get('reasons',term.get('indicator')))[:300]))
    for i,t in enumerate(j.get('threads',[])):
        fr=['%s+0x%x%s'%(imgs[x['imageIndex']].get('name','?') if x.get('imageIndex') is not None and x['imageIndex']<len(imgs) else '?',x.get('imageOffset',0),(' '+x['symbol']) if 'symbol' in x else '') for x in t.get('frames',[])]
        s=' | '.join(fr)
        if i==0 or i==j.get('faultingThread') or any(k in s for k in ('Storage','CacheDelete','AirFair','AirTraffic','MediaPlayer','Usage','MobileGestalt')):
            print('SDIAG_THREAD %s t%d%s %s :: %s'%(os.path.basename(f),i,' FAULT' if i==j.get('faultingThread') else '',t.get('queue',''),s[:1500]))
for d in ('/System/Library/PreferenceBundles/StoragePlugins','/System/Library/UsageBundles','/System/Library/SyncBundles'):
    print('SDIAG_LS %s %s'%(d,sorted(os.listdir(R+d))))
print('CC_JOB_ADDED storagediag1 (read-only)'); print('CC_RECEIPT_UPDATED unchanged')
GUEST
umount /work/root
echo INSTALL_DONE
