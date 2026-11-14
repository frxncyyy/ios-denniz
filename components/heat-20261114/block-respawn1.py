# respawn1 (OPTIONAL, not part of heat1): three launchd jobs that exit at once and are started again every 10 s for the whole uptime
# (heat1.log, 830 s: exec bluetoothd 83, fairplayd.H2 83, wifip2pd 82, each behind an xpcproxy; unified log of 2026-11-13:
# 343 / 343 / 342 "service inactive" in one boot).  None of them can work on this port:
#   bluetoothd    "Unable to match bluetooth service in device tree", "Not a valid MAC address (all 00s)" (no Bluetooth driver);
#                 launchd: "spawned because event publisher" (KeepAlive SuccessfulExit false + RunAtLoad + launch event)
#   fairplayd.H2  exits after its container lookups; launchd: "spawned because inefficient" (KeepAlive true)
#   wifip2pd      "Sandbox check failed with error: No such process"; launchd: "spawned because ipc (mach)"
# This block removes the three jobs from the launchd cache /System/Library/xpc/launchd.plist exactly as hotfix-20261114 dk1 removed
# driverkitd (Metal receipt installed_launchd_sha256 checked before and updated after).  The jobs, the full key order and the sha
# before are kept in /private/var/db/nord2-stock/respawn1.json; the undo rebuilds the dictionary in the original order.
# NOT verified on hardware: how the clients of the removed Mach services (com.apple.server.bluetooth, com.apple.fairplayd,
# com.apple.wifip2pd) behave when the name no longer exists (today they see the daemon die).  Idempotent; RESPAWN1_REFUSED = no change.
import os, json, hashlib, plistlib, base64
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
JOBS=[('/System/Library/LaunchDaemons/com.apple.bluetoothd.plist','com.apple.bluetoothd','/usr/sbin/bluetoothd'),
      ('/System/Library/LaunchDaemons/com.apple.fairplayd.H2.plist','com.apple.fairplayd.H2','/usr/sbin/fairplayd.H2'),
      ('/System/Library/LaunchDaemons/com.apple.wifip2pd.plist','com.apple.wifip2pd','/usr/libexec/wifip2pd')]
D='/private/var/db/nord2-stock'; REC=D+'/respawn1.json'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
raw=rd(LP); fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; cfg=plistlib.loads(raw)
rc=json.load(open(R+MR)); oldh=rc.get('installed_launchd_sha256')
def save_launchd(tag):
    out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
    put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
    rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
    put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid)
    print('RESPAWN1_LAUNCHD', tag, oldh[:12], '->', rc['installed_launchd_sha256'][:12])
    return sha(out)
def prog(job): return job.get('Program') or (job.get('ProgramArguments') or [None])[0]
LD=cfg.get('LaunchDaemons'); rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if oldh!=sha(raw): print('RESPAWN1_REFUSED launchd.plist differs from the Metal receipt: not touching it')
elif not isinstance(LD,dict): print('RESPAWN1_REFUSED no LaunchDaemons dictionary')
elif not UNDO:
    present=[k for k,_,_ in JOBS if k in LD]
    if rec is not None and not present: print('RESPAWN1_INSTALL already (jobs absent, receipt present)')
    elif rec is not None: print('RESPAWN1_REFUSED receipt present but', present, 'in the cache again: undo first')
    elif len(present)!=len(JOBS) or any(not isinstance(LD[k],dict) or LD[k].get('Label')!=l or prog(LD[k])!=p for k,l,p in JOBS):
        print('RESPAWN1_REFUSED jobs not found / unexpected:', [(k in LD, LD.get(k,{}).get('Label'), prog(LD.get(k,{}))) for k,_,_ in JOBS])
    else:
        keys=list(LD)
        rec={'by':'heat-20261114 respawn1','keys':[k for k,_,_ in JOBS],'order_before':keys,'launchd_sha_before':sha(raw),
             'jobs_bplist_b64':{k:base64.b64encode(plistlib.dumps(LD[k],fmt=plistlib.FMT_BINARY,sort_keys=False)).decode() for k,_,_ in JOBS}}
        os.makedirs(R+D,exist_ok=True)
        with open(R+REC,'w') as f: json.dump(rec,f,indent=1); f.flush(); os.fsync(f.fileno())
        for k,_,_ in JOBS: del LD[k]
        h=save_launchd('3 jobs removed'); rec['launchd_sha_after']=h
        with open(R+REC,'w') as f: json.dump(rec,f,indent=1); f.flush(); os.fsync(f.fileno())
        assert not any(k in plistlib.loads(rd(LP))['LaunchDaemons'] for k,_,_ in JOBS)
        print('RESPAWN1_INSTALL removed', [l for _,l,_ in JOBS], 'jobs', len(keys), '->', len(LD))
else:
    if rec is None: print('RESPAWN1_UNDO nothing to undo')
    elif any(k in LD for k in rec['keys']): print('RESPAWN1_REFUSED undo: a job is already in the cache, receipt left in place')
    else:
        jobs={k:plistlib.loads(base64.b64decode(v)) for k,v in rec['jobs_bplist_b64'].items()}; new={}
        for k in rec['order_before']:                      # the original order; keys other transactions removed since stay removed
            if k in jobs: new[k]=jobs[k]
            elif k in LD: new[k]=LD[k]
        for k,v in LD.items():                             # keys added since keep their place at the end
            if k not in new: new[k]=v
        cfg['LaunchDaemons']=new; h=save_launchd('3 jobs restored')
        os.remove(R+REC)
        if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
        print('RESPAWN1_UNDO done byte_identical_to_before=%s'%(h==rec['launchd_sha_before']))
os.sync()
