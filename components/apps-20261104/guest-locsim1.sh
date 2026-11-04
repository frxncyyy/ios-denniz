set +e
fsck.hfsplus -f -n /dev/vda | tail -2
mount -t hfsplus -o rw,force /dev/vda /work/root || exit 1
python3 - <<'GUEST'
# locsim1: locationd's own "simulated location from defaults" (locationd 20H392: CLSimulationController ctor at 0x10084e428,
# called from "Beginning Simulation Service", reads CLPreferences SimulatedLocationLatitude/Longitude/Accuracy/Type/
# Altitude/AltitudeAccuracy and, when Accuracy > 0, logs "Using simulated location from defaults" and feeds the fix).
# Keys go into locationd's prefs file (same file as LocationServicesEnabledIn8.0). Previous values -> receipt.
import os, plistlib, json
R='/work/root'
P=R+'/private/var/mobile/Library/Preferences/com.apple.locationd.plist'
K=R+'/private/var/db/nord2-locsim'; M=K+'/receipt.json'
SIM=json.load(open('/src/sim.json'))
assert set(SIM)=={'SimulatedLocationLatitude','SimulatedLocationLongitude','SimulatedLocationAccuracy','SimulatedLocationType','SimulatedLocationAltitude','SimulatedLocationAltitudeAccuracy'}, SIM
assert -90<=SIM['SimulatedLocationLatitude']<=90 and -180<=SIM['SimulatedLocationLongitude']<=180 and SIM['SimulatedLocationAccuracy']>0
def put(path, data, mode, uid, gid):
    tmp=path+'.nord2tmp'
    with open(tmp,'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
    os.chmod(tmp,mode); os.chown(tmp,uid,gid); os.replace(tmp,path)
    assert open(path,'rb').read()==data
b=open(P,'rb').read(); d=plistlib.loads(b); fmt=plistlib.FMT_BINARY if b[:8]==b'bplist00' else plistlib.FMT_XML
assert isinstance(d, dict)
if all(d.get(k)==v for k,v in SIM.items()):
    assert os.path.isfile(M), 'keys present but no receipt: review'
    print('LOCSIM1_ALREADY', SIM)
else:
    if not os.path.isfile(M):
        os.makedirs(K, exist_ok=True); os.chmod(K, 0o755)
        put(M, (json.dumps({k: d.get(k, '<absent>') for k in SIM}, indent=1, sort_keys=True)+'\n').encode(), 0o644, 0, 0)
    st=os.stat(P); d.update(SIM)
    put(P, plistlib.dumps(d, fmt=fmt), st.st_mode&0o7777, st.st_uid, st.st_gid)
    a=plistlib.loads(open(P,'rb').read())
    assert {k:v for k,v in a.items() if k not in SIM}=={k:v for k,v in plistlib.loads(b).items() if k not in SIM}
    print('LOCSIM1_INSTALLED', SIM)
print('CC_JOB_ADDED locationd simulated location from defaults (com.apple.locationd.plist)')
print('CC_RECEIPT_UPDATED unchanged')
os.sync()
GUEST
sync
umount /work/root
fsck.hfsplus -f -n /dev/vda
echo INSTALL_DONE
