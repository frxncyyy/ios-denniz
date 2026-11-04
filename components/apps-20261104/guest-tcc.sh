set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import sqlite3, time, os
R='/work/root'; db=R+'/private/var/mobile/Library/TCC/TCC.db'
st=os.stat(db)
c=sqlite3.connect(db); cur=c.cursor()
before=cur.execute("SELECT count(*) FROM access").fetchone()[0]
now=int(time.time())
rows=[('kTCCServiceCamera','com.apple.camera',0,2,2,1,None,None,0,'UNUSED',None,0,now),
      ('kTCCServiceMicrophone','com.apple.camera',0,2,2,1,None,None,0,'UNUSED',None,0,now)]
cur.executemany("INSERT OR REPLACE INTO access (service,client,client_type,auth_value,auth_reason,auth_version,csreq,policy_id,indirect_object_identifier_type,indirect_object_identifier,indirect_object_code_identity,flags,last_modified) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)",rows)
c.commit()
cur.execute("PRAGMA wal_checkpoint(TRUNCATE)"); c.commit()
got=cur.execute("SELECT service,auth_value FROM access WHERE client='com.apple.camera' ORDER BY service").fetchall()
after=cur.execute("SELECT count(*) FROM access").fetchone()[0]
c.close()
os.chown(db,st.st_uid,st.st_gid); os.chmod(db,st.st_mode&0o7777)
for ext in ('-wal','-shm'):
    p=db+ext
    if os.path.exists(p): os.chown(p,st.st_uid,st.st_gid); os.chmod(p,st.st_mode&0o7777)
print('TCC_ROWS', before, '->', after, got)
assert got==[('kTCCServiceCamera',2),('kTCCServiceMicrophone',2)], got
print('CC_JOB_ADDED tcc camera grant')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
