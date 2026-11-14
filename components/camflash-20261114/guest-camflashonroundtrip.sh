set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# camflashon (camflash-20261114): Camera.app's flash set to ON, for a test of the flash photograph without touching the screen
# (CameraUI 16.7 keeps the flash choice in mobile's com.apple.camera.plist key CAMUserPreferenceDesiredFlashMode: 0 off, 1 on, 2 auto,
# as AVCaptureFlashMode). The value it had (or that it was absent) goes to /private/var/db/nord2-stock/camflashon.json; the undo puts it
# back (the key removed if it was absent). The file keeps its owner and mode; no other key changes. Idempotent.
import json, os, plistlib
R='/work/root'; UNDO=False; K='CAMUserPreferenceDesiredFlashMode'
P='/private/var/mobile/Library/Preferences/com.apple.camera.plist'; DB='/private/var/db/nord2-stock'; REC=DB+'/camflashon.json'
def putp(path, data, st):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,R+path)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not os.path.isfile(R+P):
    print('CAMFLASHON_REFUSED no com.apple.camera.plist (Camera never ran): nothing changed' if not UNDO else 'CAMFLASHON_UNDO no plist: nothing to undo')
else:
    st=os.stat(R+P); raw=open(R+P,'rb').read(); prefs=plistlib.loads(raw); assert isinstance(prefs,dict)
    if not UNDO:
        if rec is not None and prefs.get(K)==1: print('CAMFLASHON_INSTALL already (flash on, receipt present)')
        elif rec is not None: print('CAMFLASHON_REFUSED receipt present but', K, '=', prefs.get(K), ': undo first')
        else:
            os.makedirs(R+DB,exist_ok=True)
            with open(R+REC,'w') as f: json.dump({'absent': K not in prefs, 'was': prefs.get(K), 'keys': len(prefs)},f); f.flush(); os.fsync(f.fileno())
            prefs[K]=1; putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            got=plistlib.loads(open(R+P,'rb').read()); assert got.get(K)==1 and len(got)==len(prefs)
            print('CAMFLASHON_INSTALL', K, 'was', 'absent' if json.load(open(R+REC))['absent'] else json.load(open(R+REC))['was'], '-> 1 (on)')
    else:
        if rec is None: print('CAMFLASHON_UNDO nothing to undo (no receipt)')
        else:
            if rec['absent']: prefs.pop(K, None)
            else: prefs[K]=rec['was']
            putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            os.remove(R+REC)
            if os.path.isdir(R+DB) and not os.listdir(R+DB): os.rmdir(R+DB)
            print('CAMFLASHON_UNDO', K, '->', 'absent' if rec['absent'] else rec['was'])
os.sync()
GUEST
python3 - <<'GUEST'
# camflashon (camflash-20261114): Camera.app's flash set to ON, for a test of the flash photograph without touching the screen
# (CameraUI 16.7 keeps the flash choice in mobile's com.apple.camera.plist key CAMUserPreferenceDesiredFlashMode: 0 off, 1 on, 2 auto,
# as AVCaptureFlashMode). The value it had (or that it was absent) goes to /private/var/db/nord2-stock/camflashon.json; the undo puts it
# back (the key removed if it was absent). The file keeps its owner and mode; no other key changes. Idempotent.
import json, os, plistlib
R='/work/root'; UNDO=False; K='CAMUserPreferenceDesiredFlashMode'
P='/private/var/mobile/Library/Preferences/com.apple.camera.plist'; DB='/private/var/db/nord2-stock'; REC=DB+'/camflashon.json'
def putp(path, data, st):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,R+path)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not os.path.isfile(R+P):
    print('CAMFLASHON_REFUSED no com.apple.camera.plist (Camera never ran): nothing changed' if not UNDO else 'CAMFLASHON_UNDO no plist: nothing to undo')
else:
    st=os.stat(R+P); raw=open(R+P,'rb').read(); prefs=plistlib.loads(raw); assert isinstance(prefs,dict)
    if not UNDO:
        if rec is not None and prefs.get(K)==1: print('CAMFLASHON_INSTALL already (flash on, receipt present)')
        elif rec is not None: print('CAMFLASHON_REFUSED receipt present but', K, '=', prefs.get(K), ': undo first')
        else:
            os.makedirs(R+DB,exist_ok=True)
            with open(R+REC,'w') as f: json.dump({'absent': K not in prefs, 'was': prefs.get(K), 'keys': len(prefs)},f); f.flush(); os.fsync(f.fileno())
            prefs[K]=1; putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            got=plistlib.loads(open(R+P,'rb').read()); assert got.get(K)==1 and len(got)==len(prefs)
            print('CAMFLASHON_INSTALL', K, 'was', 'absent' if json.load(open(R+REC))['absent'] else json.load(open(R+REC))['was'], '-> 1 (on)')
    else:
        if rec is None: print('CAMFLASHON_UNDO nothing to undo (no receipt)')
        else:
            if rec['absent']: prefs.pop(K, None)
            else: prefs[K]=rec['was']
            putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            os.remove(R+REC)
            if os.path.isdir(R+DB) and not os.listdir(R+DB): os.rmdir(R+DB)
            print('CAMFLASHON_UNDO', K, '->', 'absent' if rec['absent'] else rec['was'])
os.sync()
GUEST
python3 - <<'GUEST'
# camflashon (camflash-20261114): Camera.app's flash set to ON, for a test of the flash photograph without touching the screen
# (CameraUI 16.7 keeps the flash choice in mobile's com.apple.camera.plist key CAMUserPreferenceDesiredFlashMode: 0 off, 1 on, 2 auto,
# as AVCaptureFlashMode). The value it had (or that it was absent) goes to /private/var/db/nord2-stock/camflashon.json; the undo puts it
# back (the key removed if it was absent). The file keeps its owner and mode; no other key changes. Idempotent.
import json, os, plistlib
R='/work/root'; UNDO=True; K='CAMUserPreferenceDesiredFlashMode'
P='/private/var/mobile/Library/Preferences/com.apple.camera.plist'; DB='/private/var/db/nord2-stock'; REC=DB+'/camflashon.json'
def putp(path, data, st):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,R+path)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not os.path.isfile(R+P):
    print('CAMFLASHON_REFUSED no com.apple.camera.plist (Camera never ran): nothing changed' if not UNDO else 'CAMFLASHON_UNDO no plist: nothing to undo')
else:
    st=os.stat(R+P); raw=open(R+P,'rb').read(); prefs=plistlib.loads(raw); assert isinstance(prefs,dict)
    if not UNDO:
        if rec is not None and prefs.get(K)==1: print('CAMFLASHON_INSTALL already (flash on, receipt present)')
        elif rec is not None: print('CAMFLASHON_REFUSED receipt present but', K, '=', prefs.get(K), ': undo first')
        else:
            os.makedirs(R+DB,exist_ok=True)
            with open(R+REC,'w') as f: json.dump({'absent': K not in prefs, 'was': prefs.get(K), 'keys': len(prefs)},f); f.flush(); os.fsync(f.fileno())
            prefs[K]=1; putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            got=plistlib.loads(open(R+P,'rb').read()); assert got.get(K)==1 and len(got)==len(prefs)
            print('CAMFLASHON_INSTALL', K, 'was', 'absent' if json.load(open(R+REC))['absent'] else json.load(open(R+REC))['was'], '-> 1 (on)')
    else:
        if rec is None: print('CAMFLASHON_UNDO nothing to undo (no receipt)')
        else:
            if rec['absent']: prefs.pop(K, None)
            else: prefs[K]=rec['was']
            putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            os.remove(R+REC)
            if os.path.isdir(R+DB) and not os.listdir(R+DB): os.rmdir(R+DB)
            print('CAMFLASHON_UNDO', K, '->', 'absent' if rec['absent'] else rec['was'])
os.sync()
GUEST
python3 - <<'GUEST'
# camflashon (camflash-20261114): Camera.app's flash set to ON, for a test of the flash photograph without touching the screen
# (CameraUI 16.7 keeps the flash choice in mobile's com.apple.camera.plist key CAMUserPreferenceDesiredFlashMode: 0 off, 1 on, 2 auto,
# as AVCaptureFlashMode). The value it had (or that it was absent) goes to /private/var/db/nord2-stock/camflashon.json; the undo puts it
# back (the key removed if it was absent). The file keeps its owner and mode; no other key changes. Idempotent.
import json, os, plistlib
R='/work/root'; UNDO=True; K='CAMUserPreferenceDesiredFlashMode'
P='/private/var/mobile/Library/Preferences/com.apple.camera.plist'; DB='/private/var/db/nord2-stock'; REC=DB+'/camflashon.json'
def putp(path, data, st):
    tmp=R+path+'.nord2tmp'
    if os.path.lexists(tmp): os.remove(tmp)
    with open(tmp,'xb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,st.st_mode&0o7777); os.chown(tmp,st.st_uid,st.st_gid); os.replace(tmp,R+path)
rec=json.load(open(R+REC)) if os.path.exists(R+REC) else None
if not os.path.isfile(R+P):
    print('CAMFLASHON_REFUSED no com.apple.camera.plist (Camera never ran): nothing changed' if not UNDO else 'CAMFLASHON_UNDO no plist: nothing to undo')
else:
    st=os.stat(R+P); raw=open(R+P,'rb').read(); prefs=plistlib.loads(raw); assert isinstance(prefs,dict)
    if not UNDO:
        if rec is not None and prefs.get(K)==1: print('CAMFLASHON_INSTALL already (flash on, receipt present)')
        elif rec is not None: print('CAMFLASHON_REFUSED receipt present but', K, '=', prefs.get(K), ': undo first')
        else:
            os.makedirs(R+DB,exist_ok=True)
            with open(R+REC,'w') as f: json.dump({'absent': K not in prefs, 'was': prefs.get(K), 'keys': len(prefs)},f); f.flush(); os.fsync(f.fileno())
            prefs[K]=1; putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            got=plistlib.loads(open(R+P,'rb').read()); assert got.get(K)==1 and len(got)==len(prefs)
            print('CAMFLASHON_INSTALL', K, 'was', 'absent' if json.load(open(R+REC))['absent'] else json.load(open(R+REC))['was'], '-> 1 (on)')
    else:
        if rec is None: print('CAMFLASHON_UNDO nothing to undo (no receipt)')
        else:
            if rec['absent']: prefs.pop(K, None)
            else: prefs[K]=rec['was']
            putp(P, plistlib.dumps(prefs, fmt=plistlib.FMT_BINARY), st)
            os.remove(R+REC)
            if os.path.isdir(R+DB) and not os.listdir(R+DB): os.rmdir(R+DB)
            print('CAMFLASHON_UNDO', K, '->', 'absent' if rec['absent'] else rec['was'])
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
