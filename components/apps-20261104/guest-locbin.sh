set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
python3 - <<'PY'
import re,subprocess
d=open('/work/root/usr/libexec/locationd','rb').read()
print('SIZE',len(d))
for pat in (rb'CoreTelephony',rb'commcenter',rb'CTTelephonyNetworkInfo',rb'CTCellularData',rb'CoreTelephonyClient',rb'CLEmergencyControllerAdapter[A-Za-z]*',rb'CLTelephony[A-Za-z]*'):
    m=sorted(set(re.findall(pat,d)))[:12]
    print('PAT',pat.decode(),len(re.findall(pat,d)),[x.decode(errors='replace') for x in m][:12])
# dylib list
import struct
off=0
magic=struct.unpack('<I',d[:4])[0]
print('MAGIC',hex(magic))
PY
umount /work/root
echo INSPECT_DONE
