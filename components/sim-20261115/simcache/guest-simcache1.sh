set +e
# simcache1 (gpu/build/sim-20261115/simcache): installs /usr/libexec/n2simcache (one-shot root job com.nord2.simcache: with no SIM, removes
# the stale carrier bundle links + com.apple.carrier_1 and restarts CommCenter once, see n2simcache.c) and its state dir
# /private/var/db/nord2-simcache. The payload's plan.json 'mode' selects: install (binary + job + dir; switches as planned),
# switches (only the switch files: disabled / norestart / keepkey / dryrun = exactly the planned set, made-by-simcache marker).
# Idempotent (SIMCACHE1_ALREADY). Every check runs before the first write. Record /private/var/db/nord2-simcache/receipt.json.
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
import hashlib, json, os, plistlib
R='/work/root'; BIN='/usr/libexec/n2simcache'; D='/private/var/db/nord2-simcache'; REC=D+'/receipt.json'
LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'; JK='/System/Library/LaunchDaemons/com.nord2.simcache.plist'
SWITCHES=['disabled','norestart','keepkey','dryrun']; MARK=b'made-by-simcache\n'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
plan=json.load(open('/src/plan.json')); mode=plan['mode']; assert mode in ('install','switches'), mode
want=set(plan.get('switches',[])); assert want <= set(SWITCHES), want
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
# ---- checks ----
for s in SWITCHES:                                      # a foreign file with a switch name: not ours, never touched
    p=D+'/'+s
    assert not os.path.lexists(R+p) or rd(p)==MARK or (s=='norestart' and rd(p)==b'auto\n'), 'SIMCACHE1_REFUSED %s is not ours'%p
if mode=='switches':
    assert rec is not None, 'SIMCACHE1_REFUSED not installed: install simcache1 first'
else:
    b=open('/src/n2simcache','rb').read(); assert sha(b)==plan['n2simcache_sha256'], 'SIMCACHE1_REFUSED payload binary is not the planned one'
    raw=rd(LP); cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
    rc=json.load(open(R+MR)); assert rc.get('installed_launchd_sha256')==sha(raw), 'SIMCACHE1_REFUSED launchd.plist differs from the Metal receipt: not touching it'
    assert not any(isinstance(j,dict) and j.get('Label')==plan['job']['Label'] for k,j in LD.items() if k!=JK), 'SIMCACHE1_REFUSED label used elsewhere'
    if os.path.lexists(R+BIN): assert rec is not None, 'SIMCACHE1_REFUSED %s exists and is not ours'%BIN
# ---- writes ----
changed=[]
if mode=='install':
    if not os.path.isdir(R+D): os.makedirs(R+D); os.chmod(R+D,0o755); os.chown(R+D,0,0); changed.append(D)
    if not (os.path.exists(R+BIN) and rd(BIN)==b): put(BIN,b,0o755,0,0); changed.append(BIN)
    raw=rd(LP); cfg=plistlib.loads(raw); LD=cfg['LaunchDaemons']
    if LD.get(JK)!=plan['job']:
        prev=LD.get(JK); LD[JK]=plan['job']
        fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); lst=os.stat(R+LP)
        put(LP,out,lst.st_mode&0o7777,lst.st_uid,lst.st_gid); assert plistlib.loads(rd(LP))==cfg
        rc=json.load(open(R+MR)); rc['installed_launchd_sha256']=sha(out); mst=os.stat(R+MR)
        put(MR,(json.dumps(rc,indent=2)+'\n').encode(),mst.st_mode&0o7777,mst.st_uid,mst.st_gid); changed.append(LP)
# switch files: exactly the planned set among the ones we made (a 'norestart' the helper wrote itself after a failed link check
# ("auto") is kept unless this payload lists 'norestart-clear')
for s in SWITCHES:
    p=D+'/'+s; there=os.path.lexists(R+p)
    if s in want and not there: put(p,MARK,0o644,0,0); changed.append(p)
    elif s not in want and there and (rd(p)==MARK or (s=='norestart' and 'norestart-clear' in plan.get('flags',[]))): os.remove(R+p); changed.append(p+' (removed)')
new={'payload':'simcache1','n2simcache_sha256':plan['n2simcache_sha256'] if mode=='install' else (rec or {}).get('n2simcache_sha256'),
     'job':JK,'switches':sorted(s for s in SWITCHES if os.path.lexists(R+D+'/'+s))}
nb=(json.dumps(new,indent=1)+'\n').encode()
if not (os.path.exists(R+REC) and rd(REC)==nb): put(REC,nb,0o644,0,0)
print('SIMCACHE1_SWITCHES', new['switches'])
print(('SIMCACHE1_INSTALLED' if changed else 'SIMCACHE1_ALREADY'), mode, changed)
os.sync()
GUEST
rc=$?
[ $rc -eq 0 ] && echo 'SIMCACHE1_OK'
sync
umount /work/root
echo 'CC_JOB_ADDED simcache (com.nord2.simcache launchd job)'
echo 'CC_RECEIPT_UPDATED simcache (launchd.plist + owned files)'
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
