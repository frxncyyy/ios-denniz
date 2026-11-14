# diag1 (diagnostic only, no fix): the idle heat after cc-txn-wall4 is mostly ONE coreduetd thread at ~80% of core 5
# (kernel, hot1.log line 34467: 'process coreduetd[107] thread 10350 caught burning CPU! ... 80% over ~111 seconds'; still
# there at 1 h uptime in hot2.log; aggregated[208] did the same for 113 s earlier). The USB log has no per-thread stacks for
# it. The existing debug job com.nord2.locdiag already runs /usr/local/libexec/n2sample at every boot (thread states + pc/lr/
# backtraces of the named processes into the kernel log, lines 'N2SAMPLE ...'): this block adds 'coreduetd' and 'aggregated' to
# its process list and raises --repeat from 4 to 10 (samples at 150 s .. 690 s after boot), in /System/Library/xpc/launchd.plist
# (Metal receipt sha checked/updated as poster1). Original arguments in /private/var/db/nord2-stock/diag1.json; undo restores.
import os, json, hashlib, plistlib
R='/work/root'; LP='/System/Library/xpc/launchd.plist'; MR='/private/var/db/nord2-metal/receipt.json'
KEY='/System/Library/LaunchDaemons/com.nord2.locdiag.plist'; TOOL='/usr/local/libexec/n2sample'; ADD=['coreduetd','aggregated']; REPEAT='10'
D='/private/var/db/nord2-stock'; REC=D+'/diag1.json'
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
    print('DIAG1_LAUNCHD', tag, oldh[:12], '->', rc['installed_launchd_sha256'][:12])
job=cfg.get('LaunchDaemons',{}).get(KEY); rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
args=job.get('ProgramArguments') if isinstance(job,dict) else None
if oldh!=sha(raw): print('DIAG1_REFUSED launchd.plist differs from the Metal receipt: not touching it')
elif not isinstance(args,list) or not args or args[0]!=TOOL or '--repeat' not in args[:-1]: print('DIAG1_REFUSED n2sample job not found / unexpected')
elif not UNDO:
    new=list(args); new[new.index('--repeat')+1]=REPEAT; new+=[a for a in ADD if a not in new]
    if rec is None:
        rec={'by':'payload-hotfix1 diag1','key':KEY,'args_before':args}; os.makedirs(R+D,exist_ok=True)
        with open(R+REC,'w') as f: json.dump(rec,f,indent=1); f.flush(); os.fsync(f.fileno())
    if new!=args: job['ProgramArguments']=new; save_launchd('n2sample args')
    print('DIAG1_INSTALL', ' '.join(plistlib.loads(rd(LP))['LaunchDaemons'][KEY]['ProgramArguments'][1:]))
else:
    if rec is None: print('DIAG1_UNDO nothing to undo')
    else:
        if args!=rec['args_before']: job['ProgramArguments']=rec['args_before']; save_launchd('n2sample args restored')
        os.remove(R+REC)
        if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
        print('DIAG1_UNDO done')
os.sync()
