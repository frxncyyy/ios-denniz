#!/bin/bash
set -euo pipefail
mount -t hfsplus -o ro /dev/vda /work/root
python3 - <<'PY'
import json,hashlib,os,stat
records=json.load(open('/build/critical-files.json'))
for record in records:
 name,rel=record['path'].split('/root/',1)
 prefix='/work/root/' if name=='OS' else '/work/root/private/preboot/Cryptexes/'+('OS/' if name=='SystemOS' else 'App/')
 p=prefix+rel;st=os.stat(p)
 assert (st.st_size,oct(stat.S_IMODE(st.st_mode)),st.st_uid,st.st_gid)==(record['size'],record['mode'],record['uid'],record['gid']),p
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(8*1024*1024),b''):h.update(b)
 assert h.hexdigest()==record['sha256'],p
print('ALL_CRITICAL_FILES_MATCH',len(records))
PY
umount /work/root
