set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
ls -la /work/root/usr/lib/dyld
python3 - <<'PY'
import base64,zlib
d=open('/work/root/usr/lib/dyld','rb').read()
z=zlib.compress(d,9)
print('DYLDB64BEGIN',len(d),len(z))
b=base64.b64encode(z).decode()
for i in range(0,len(b),4000): print('D:'+b[i:i+4000])
print('DYLDB64END')
PY
umount /work/root
echo INSPECT_DONE
