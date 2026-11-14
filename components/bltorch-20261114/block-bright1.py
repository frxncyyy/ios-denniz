# bright1: automatic brightness off in backboardd's preferences (defensive; q10jch run 2026-11-13).
# The Nord 2 has no ambient light sensor service (SweetHID publishes proximity only; MobileGestalt "ambient-light-sensor" is cached
# false, and CoreBrightness' DisplaySetProperty refuses "DisplayBrightnessAuto" without it: "Refuse to turn on AAB without ALS").
# So this is not the cause of the dead slider (kernel 0501 is the fix); it only makes the stored preference say what the hardware
# can do: BKEnableALS = false in /private/var/mobile/Library/Preferences/com.apple.backboardd.plist (BKSLocalDefaults "ALSEnabled",
# domain com.apple.backboardd -- the key Settings > Accessibility > Display > Auto-Brightness writes). No other key is touched
# (the saved slider level SBBacklightLevel2 stays the user's). Receipt /private/var/db/nord2-stock/bright1.json (the key's state
# before and the file as it was); the undo restores exactly that -- the original bytes when nothing else changed in the file since,
# else only the key. Idempotent; BRIGHT1_REFUSED = no change.
import os, json, hashlib, plistlib, base64
R='/work/root'; PL='/private/var/mobile/Library/Preferences/com.apple.backboardd.plist'; KEY='BKEnableALS'
D='/private/var/db/nord2-stock'; REC=D+'/bright1.json'
sha=lambda b: hashlib.sha256(b).hexdigest(); rd=lambda p: open(R+p,'rb').read()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert rd(path)==data
def drop_rec():
    os.remove(R+REC)
    if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not os.path.isfile(R+PL): print('BRIGHT1_REFUSED no com.apple.backboardd.plist (not making one)')
else:
    raw=rd(PL); st=os.stat(R+PL)
    try: cfg=plistlib.loads(raw)
    except Exception as e: cfg=None
    fmt=plistlib.FMT_BINARY if raw[:6]==b'bplist' else plistlib.FMT_XML
    if not isinstance(cfg,dict): print('BRIGHT1_REFUSED com.apple.backboardd.plist is not a dictionary')
    elif not UNDO:
        if rec is not None and cfg.get(KEY) is False: print('BRIGHT1_INSTALL already (BKEnableALS false, receipt present)')
        elif rec is not None: print('BRIGHT1_REFUSED receipt present but BKEnableALS is', repr(cfg.get(KEY)), ': undo first')
        elif cfg.get(KEY) is False: print('BRIGHT1_INSTALL BKEnableALS already false (no receipt): nothing to change')
        elif KEY in cfg and not isinstance(cfg[KEY],bool): print('BRIGHT1_REFUSED BKEnableALS has an unexpected value', repr(cfg[KEY]))
        else:
            rec={'by':'bltorch-20261114 bright1','path':PL,'key':KEY,'had_key':KEY in cfg,'old_value':cfg.get(KEY),'sha_before':sha(raw),'file_before_b64':base64.b64encode(raw).decode()}
            os.makedirs(R+D,exist_ok=True)
            with open(R+REC,'w') as f: json.dump(rec,f,indent=1); f.flush(); os.fsync(f.fileno())
            cfg[KEY]=False; out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False)
            put(PL,out,st.st_mode&0o7777,st.st_uid,st.st_gid); assert plistlib.loads(rd(PL))==cfg
            print('BRIGHT1_INSTALL BKEnableALS %s -> false (%d keys, level SBBacklightLevel2 %r untouched)'%('true' if rec['had_key'] else 'absent',len(cfg),cfg.get('SBBacklightLevel2')))
    else:
        if rec is None: print('BRIGHT1_UNDO nothing to undo')
        elif cfg.get(KEY) is not False:
            drop_rec(); print('BRIGHT1_UNDO BKEnableALS is', repr(cfg.get(KEY)), '(changed since, e.g. in Settings): receipt removed, file left as it is')
        else:
            if rec['had_key']: cfg[KEY]=rec['old_value']
            else: del cfg[KEY]
            out=plistlib.dumps(cfg,fmt=fmt,sort_keys=False); orig=base64.b64decode(rec['file_before_b64'])
            if sha(orig)==rec['sha_before'] and plistlib.loads(orig)==cfg: out=orig      # nothing else changed since: the file as it was, byte for byte
            put(PL,out,st.st_mode&0o7777,st.st_uid,st.st_gid); assert plistlib.loads(rd(PL))==cfg
            drop_rec(); print('BRIGHT1_UNDO BKEnableALS restored (%s) byte_identical_to_before=%s'%('true' if rec['had_key'] else 'absent', sha(out)==rec['sha_before']))
os.sync()
