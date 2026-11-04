set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import glob,json,os
fs=sorted(glob.glob('/work/root/private/var/mobile/Library/Logs/CrashReporter/SubwaySurf-*.ips'))
print('FILES',len(fs))
for f in fs:
    raw=open(f,errors='replace').read()
    try:
        head,body=raw.split('\n',1); h=json.loads(head); b=json.loads(body)
    except Exception as e:
        print('PARSE',os.path.basename(f),e); continue
    ex=b.get('exception',{}); term=b.get('termination',{})
    print('REPORT',os.path.basename(f),h.get('timestamp'),ex.get('type'),ex.get('codes'),term.get('indicator'),str(term.get('details',''))[:120])
PY
echo ---
python3 - <<'PY'
import glob,json,os
fs=sorted(glob.glob('/work/root/private/var/mobile/Library/Logs/CrashReporter/SubwaySurf-*.ips'))
for f in fs[-3:]:
    raw=open(f,errors='replace').read(); head,body=raw.split('\n',1); b=json.loads(body)
    imgs=b.get('usedImages',[])
    print('== ',os.path.basename(f),'faulting',b.get('faultingThread'))
    for i,t in enumerate(b.get('threads',[])):
        fr=t.get('frames',[])
        names=[]
        for x in fr[:7]:
            im=imgs[x['imageIndex']] if 'imageIndex' in x and x['imageIndex']<len(imgs) else {}
            names.append('%s:%s+%s'%(im.get('name','?'),x.get('symbol','?'),x.get('symbolLocation','?')))
        print(' T%d %s %s'%(i,t.get('name',t.get('queue','')),t.get('triggered','')), ' | '.join(names))
PY
umount /work/root
echo INSPECT_DONE
