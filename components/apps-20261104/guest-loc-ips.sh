set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import glob,json,os
fs=sorted(glob.glob('/work/root/private/var/mobile/Library/Logs/CrashReporter/locationd-*.ips'))
print('FILES',[os.path.basename(f) for f in fs])
for f in fs[-1:]:
    raw=open(f,errors='replace').read(); head,body=raw.split('\n',1); b=json.loads(body)
    imgs=b.get('usedImages',[])
    print('== ',os.path.basename(f),'pid',b.get('pid'),b.get('captureTime'),'faulting',b.get('faultingThread'))
    for i,t in enumerate(b.get('threads',[])):
        names=[]
        for x in t.get('frames',[])[:22]:
            im=imgs[x['imageIndex']] if 'imageIndex' in x and x['imageIndex']<len(imgs) else {}
            names.append('%s:%s'%(im.get('name','?'),x.get('symbol','?')))
        print(' T%d %s %s'%(i,t.get('name',t.get('queue','')),'TRIG' if t.get('triggered') else ''), ' | '.join(names))
PY
umount /work/root
echo INSPECT_DONE
