# posteroff1: take poster providers that cannot run on this port out of LaunchServices, without deleting anything.
# AegirPoster (Astronomy, /System/Library/CoreServices/AegirProxyApp.app/PlugIns/AegirPoster.appex): hot3.log (clean boot) -
#   2 launches abort with an uncaught exception right after 'a Metal device is made here' (std::terminate under a
#   FrontBoardServices scene block), 16 more are killed with 'extension check-in timeout' while their main thread sleeps in
#   libredwoodair new_bo() <- panfrost_open_device() (the 100 x 100 ms retry of a kIOReturnIOError: the kernel has no free GPU
#   context, N2C_MAX_CTX 8 = 7 clients); PosterBoard relaunches it every ~10 s (19 execs in 2 minutes).
# The 'lean' list adds PridePosterExtension and WeatherPoster: they run, but each holds one of the 7 GPU contexts
# (PridePosterExtension[245] took the last one in hot3.log, after which every other Metal client waited 10 s per attempt).
# How: the extension's Info.plist gets the extension point 'com.apple.posterkit.provider.nord2-off' (unknown to the system)
# instead of 'com.apple.posterkit.provider'. LaunchServices drops app extensions whose point it does not know - the very
# behaviour that hid all posters before wallpaper1 - and PosterBoard asks LaunchServices for its providers. The original
# Info.plist is kept in /private/var/db/nord2-stock/posteroff1/ and put back by the undo. The LaunchServices caches are moved
# aside (as stock1/wallpaper1) so lsd rescans. OFF = list of Info.plist paths, set by the guest. Idempotent; *_REFUSED = no change.
import os, json, glob, hashlib, plistlib
R='/work/root'; POINT='com.apple.posterkit.provider'; DEAD=POINT+'.nord2-off'
D='/private/var/db/nord2-stock'; REC=D+'/posteroff1.json'; BK=D+'/posteroff1'
LSG='/private/var/mobile/Containers/Data/InternalDaemon/*/Library/Caches/*LaunchServices-*.csstore'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
def lsaside(tag):
    moved=[]
    for f in glob.glob(R+LSG):
        dst=f+'.'+tag
        if os.path.lexists(dst): os.remove(dst)
        os.rename(f,dst); moved.append(os.path.basename(f))
    return moved
def swap(o, a, b):
    """every string value a under o becomes b; returns how many"""
    n=0
    for k in list(o):
        if isinstance(o[k],dict): n+=swap(o[k],a,b)
        elif o[k]==a: o[k]=b; n+=1
    return n
def saverec(rec):
    with open(R+REC,'w') as f: json.dump(rec,f,indent=1); f.flush(); os.fsync(f.fileno())
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not UNDO:
    bad=[]
    for p in OFF:
        try:
            raw=rd(p); ip=plistlib.loads(raw); ext=ip.get('NSExtension'); e=(rec or {}).get('files',{}).get(p)
            if not isinstance(ext,dict): bad.append(p+': no NSExtension')
            elif e is None and ext.get('NSExtensionPointIdentifier')!=POINT: bad.append(p+': point is %r'%ext.get('NSExtensionPointIdentifier'))
            elif e is not None and (sha(raw) not in (e['sha_before'],e.get('sha_after')) or not os.path.exists(R+e['backup']) or sha(rd(e['backup']))!=e['sha_before']): bad.append(p+': receipt does not match the files on disk')
        except Exception as x: bad.append('%s: %r'%(p,x))
    if not OFF or bad: print('POSTEROFF1_REFUSED', bad or 'empty list')
    else:
        if rec is None: rec={'by':'payload-hotfix1 posteroff1','point':POINT,'dead_point':DEAD,'files':{}}
        os.makedirs(R+BK,exist_ok=True); os.chmod(R+BK,0o755); changed=[]
        for p in OFF:
            raw=rd(p); st=os.stat(R+p); e=rec['files'].get(p)
            if e is None:
                n=0
                while os.path.lexists(R+BK+'/%d-Info.plist'%n): n+=1
                e={'sha_before':sha(raw),'backup':BK+'/%d-Info.plist'%n}
                put(e['backup'],raw,st.st_mode&0o7777,st.st_uid,st.st_gid); rec['files'][p]=e; saverec(rec)
            if sha(raw)==e['sha_before']:
                ip=plistlib.loads(raw); n=swap(ip['NSExtension'],POINT,DEAD); assert n>=1
                out=plistlib.dumps(ip,fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML,sort_keys=False)
                put(p,out,st.st_mode&0o7777,st.st_uid,st.st_gid); e['sha_after']=sha(out); e['replaced']=n; saverec(rec); changed.append(p)
            chk=plistlib.loads(rd(p)); assert chk['NSExtension']['NSExtensionPointIdentifier']==DEAD and POINT not in json.dumps(chk['NSExtension']).replace(DEAD,'')
        ls=lsaside('pre-posteroff1') if changed else []
        print('POSTEROFF1_INSTALL off=%d changed=%d %s ls_caches_aside=%s'%(len(rec['files']),len(changed),[p.split('/')[-2] for p in changed],ls))
else:
    if rec is None: print('POSTEROFF1_UNDO nothing to undo')
    else:
        bad=[p for p,e in rec['files'].items() if not os.path.exists(R+e['backup']) or sha(rd(e['backup']))!=e['sha_before'] or not os.path.exists(R+p) or sha(rd(p)) not in (e['sha_before'],e.get('sha_after'))]
        if bad: print('POSTEROFF1_REFUSED undo: receipt does not match the files on disk', bad)
        else:
            for p,e in rec['files'].items():
                st=os.stat(R+p); orig=rd(e['backup'])
                if rd(p)!=orig: put(p,orig,st.st_mode&0o7777,st.st_uid,st.st_gid)
                os.remove(R+e['backup'])
            os.rmdir(R+BK); os.remove(R+REC)
            if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
            print('POSTEROFF1_UNDO done restored=%d ls_caches_aside=%s'%(len(rec['files']),lsaside('pre-posteroff1-undo')))
os.sync()
