set +e
# payload-storage-purge2: removes exactly the files listed in /src/purge2-list.json (generated from the rootfs mirror),
# skipping any that are already gone; refuses any path outside the allowed cleanup areas.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, json, re
R='/work/root'
L=json.load(open('/src/purge2-list.json'))
ALLOW=re.compile(r'^/private/var/(root/Library/Caches/com\.apple\.coresymbolicationd/[^/]+'
                 r'|db/uuidtext/dsc/[0-9A-F]{32}'
                 r'|db/diagnostics/(Persist|Special|Signpost)/[0-9a-f]{16}\.tracev3'
                 r'|db/nord2-metal/(libredwoodair\.dylib|Nord2Metal)\.pre-(vt\d*|psiz|gpuisp|gpuycbcr|ycbcr|planar|gpuview)'
                 r'|mobile/Library/Logs/CrashReporter/\.?(OTAUpdate|awdd|JetsamEvent|SiriSearchFeedback)-[^/]+'
                 r'|mobile/Library/Caches/(CloudKit|com\.apple\.rtcreporting|com\.apple\.proactive\.eventtracker|com\.apple\.geod|com\.apple\.feedbacklogger|com\.apple\.parsecd|GeoServices|com\.apple\.QuickLook\.thumbnailcache|GameKit|com\.apple\.NewDeviceOutreach|com\.apple\.Safari\.SafeBrowsing)/.+)$')
KEEP=('/private/var/db/uuidtext/dsc/8F970452C9E33E88B114FEB1864BC9E0',)
bad=[x['path'] for x in L if not ALLOW.match(x['path']) or '/../' in x['path'] or x['path'] in KEEP]
if bad:
    print('PURGE2_REFUSED path outside the cleanup areas: %s (nothing removed)'%bad[:3]); raise SystemExit(0)
n=b=0; cat={}
for x in L:
    p=R+x['path']
    if os.path.islink(p) or not os.path.isfile(p): continue
    s=os.path.getsize(p); os.remove(p); n+=1; b+=s; cat[x['cat']]=cat.get(x['cat'],0)+s
    print('PURGE2_REMOVED %d %s'%(s,x['path']))
for c,v in sorted(cat.items()): print('PURGE2_CATEGORY %s %d'%(c,v))
st=os.statvfs(R)
print('PURGE2_TOTAL files=%d bytes=%d free_after=%d'%(n,b,st.f_bavail*st.f_frsize))
print('CC_JOB_ADDED storage: purge2 caches, stale uuidtext, old logs, obsolete backups removed')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
