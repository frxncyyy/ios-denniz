set +e
# payload-storage1: Settings > General > iPhone Storage. Moves out of the load paths the 4 bundles that crash/block the page:
#  PodcastsUsagePlugin (StoragePlugins): Swift trap PodcastsUI+0x8dda8 when [[MPMediaLibrary defaultMediaLibrary] databasePath] is nil (Preferences SIGTRAP);
#  AirFair.syncBundle: -[FPAirFair init] on the MAIN thread (ATDeviceDiskUsageProvider) waits in MobileGestalt for the 'marconi-wifi' IOService (absent on Nord2);
#  VideosUsage / MusicUsage (UsageBundles): MPMediaQuery waits on medialibraryd while StorageSettings waits for all usage bundles (TV/Music/Podcasts apps are not installed).
# Kept under /private/var/db/nord2-storage/disabled/<original path>. Idempotent.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import os, json
R='/work/root'; UNDO=False
D='/private/var/db/nord2-storage'
B=['/System/Library/PreferenceBundles/StoragePlugins/PodcastsUsagePlugin.bundle',
   '/System/Library/SyncBundles/AirFair.syncBundle',
   '/System/Library/UsageBundles/VideosUsage.bundle',
   '/System/Library/UsageBundles/MusicUsage.bundle']
did=[]
for b in B:
    src, dst = R+b, R+D+'/disabled'+b
    if UNDO: src, dst = dst, src
    if os.path.isdir(src) and not os.path.lexists(dst):
        os.makedirs(os.path.dirname(dst), exist_ok=True); os.rename(src, dst); did.append(b)
    elif os.path.isdir(dst) and not os.path.lexists(src): pass
    else: print('STORAGE1_REFUSED unexpected state for %s (src %s dst %s): nothing more changed'%(b, os.path.lexists(src), os.path.lexists(dst))); break
if UNDO:
    for root, ds, fs in os.walk(R+D+'/disabled', topdown=False):
        if not ds and not fs and os.path.isdir(root): os.rmdir(root)
    if os.path.isdir(R+D+'/disabled') and not os.listdir(R+D+'/disabled'): os.rmdir(R+D+'/disabled')
    if os.path.exists(R+D+'/storage1.json'): os.remove(R+D+'/storage1.json')
    if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
else:
    if did or not os.path.exists(R+D+'/storage1.json'):
        json.dump({'disabled':B,'by':'payload-storage1'}, open(R+D+'/storage1.json','w'), indent=1)
print('STORAGE1_%s moved=%d %s'%('UNDO' if UNDO else 'INSTALL', len(did), did))
print('CC_JOB_ADDED storage1%s'%(' undo' if UNDO else ': storage page plugins disabled'))
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
