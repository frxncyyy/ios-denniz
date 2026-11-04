set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import glob,json,os
f=sorted(glob.glob('/work/root/private/var/mobile/Library/Logs/CrashReporter/SubwaySurf-*.ips'))[-1]
raw=open(f,errors='replace').read(); head,body=raw.split('\n',1); b=json.loads(body)
imgs=b.get('usedImages',[])
t=b['threads'][0]
for x in t['frames']:
    im=imgs[x['imageIndex']] if 'imageIndex' in x and x['imageIndex']<len(imgs) else {}
    print('F %s %s+%s'%(im.get('name','?'),x.get('symbol','?'),x.get('symbolLocation','?')))
print('IMAGES',[i.get('name') for i in imgs if not str(i.get('path','')).startswith(('/usr/lib','/System'))])
print('MODS',[ (i.get('name'),i.get('path')) for i in imgs if 'Frameworks' in str(i.get('path',''))])
print(b.get('termination'))
PY
umount /work/root
echo INSPECT_DONE
