# hapticpref: Settings > Sounds & Haptics > System Haptics on (CFPreferences domain com.apple.preferences.sounds, key
# "effects-haptic" = true; SoundsAndHapticsSettings 20H392 reads it at 0x2023a777c).  The mirror has no such plist (2026-11-14), so
# the switch would read as off [I: whether a missing key means off was read from the Settings bundle, not seen on a device].
# Only acts when the key is absent: a missing file is created (mobile 501:501, 0600, binary plist, the owner/mode of its
# neighbours); an existing file without the key gets the key (the old bytes kept in the receipt for a byte-identical undo).
# A key already present (the user's choice) is never changed.  Receipt /private/var/db/nord2-stock/hapticpref.json.
# Idempotent; HAPTICPREF_REFUSED = no change.
import os, json, hashlib, plistlib, base64
R='/work/root'
P='/private/var/mobile/Library/Preferences/com.apple.preferences.sounds.plist'
D='/private/var/db/nord2-stock'; REC=D+'/hapticpref.json'; KEY='effects-haptic'
sha=lambda b: hashlib.sha256(b).hexdigest()
def put(path, data, mode, uid, gid):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,R+path)
    assert open(R+path,'rb').read()==data
def rmrec():
    os.remove(R+REC)
    if os.path.isdir(R+D) and not os.listdir(R+D): os.rmdir(R+D)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
exists=os.path.isfile(R+P)
if not UNDO:
    if rec is not None: print('HAPTICPREF_INSTALL already (receipt present)')
    elif not os.path.isdir(R+os.path.dirname(P)): print('HAPTICPREF_REFUSED no mobile Preferences directory')
    elif not exists:
        new=plistlib.dumps({KEY: True}, fmt=plistlib.FMT_BINARY)
        os.makedirs(R+D,exist_ok=True)
        with open(R+REC,'w') as f: json.dump({'by':'haptic-20261114 hapticpref','path':P,'created':True,'sha_after':sha(new)},f,indent=1); f.flush(); os.fsync(f.fileno())
        put(P,new,0o600,501,501)
        print('HAPTICPREF_INSTALL created %s with %s = true (%d bytes)'%(P,KEY,len(new)))
    else:
        raw=open(R+P,'rb').read(); st=os.stat(R+P)
        try: d=plistlib.loads(raw)
        except Exception as e: d=None; print('HAPTICPREF_REFUSED not a property list (%s)'%type(e).__name__)
        if d is None: pass
        elif not isinstance(d,dict): print('HAPTICPREF_REFUSED not a dictionary')
        elif KEY in d: print('HAPTICPREF_INSTALL %s already set to %r: left as it is'%(KEY,d[KEY]))
        else:
            d[KEY]=True; new=plistlib.dumps(d, fmt=plistlib.FMT_BINARY)
            os.makedirs(R+D,exist_ok=True)
            with open(R+REC,'w') as f: json.dump({'by':'haptic-20261114 hapticpref','path':P,'created':False,'old_b64':base64.b64encode(raw).decode(),'mode':st.st_mode&0o7777,'uid':st.st_uid,'gid':st.st_gid,'sha_before':sha(raw),'sha_after':sha(new)},f,indent=1); f.flush(); os.fsync(f.fileno())
            put(P,new,st.st_mode&0o7777,st.st_uid,st.st_gid)
            print('HAPTICPREF_INSTALL added %s = true to the existing file'%KEY)
else:
    if rec is None: print('HAPTICPREF_UNDO nothing to undo')
    elif not exists: rmrec(); print('HAPTICPREF_UNDO the file is gone: receipt removed')
    else:
        raw=open(R+P,'rb').read()
        if sha(raw)!=rec['sha_after']: rmrec(); print('HAPTICPREF_UNDO the file changed since (the user or Settings wrote it): left as it is, receipt removed')
        elif rec['created']: os.remove(R+P); rmrec(); print('HAPTICPREF_UNDO removed the file we created')
        else:
            old=base64.b64decode(rec['old_b64']); put(P,old,rec['mode'],rec['uid'],rec['gid']); rmrec()
            print('HAPTICPREF_UNDO file restored byte_identical=%s'%(sha(old)==rec['sha_before']))
os.sync()
