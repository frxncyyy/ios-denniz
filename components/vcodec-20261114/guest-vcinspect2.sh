set +e
mount -t hfsplus -o ro,force /dev/vda /work/root
R=/work/root
python3 - <<'PY'
import re
L=open('/work/root/private/var/db/nord2-vcodec/vcodecd.log','rb').read().decode('latin1').splitlines()
boots=[]; cur=None
for l in L:
    if ' start dev=' in l: cur=[l]; boots.append(cur); continue
    if cur is not None and re.search(r'session|first picture|open|ended|enc:|dec: (START|header|session)|error|fail|timeout|BUSY|restart', l, re.I) and 'n2a32_mem' not in l and 'N2VHOST' not in l:
        cur.append(l)
print('boots', len(boots))
for i,b in enumerate(boots):
    if len(b)>1: print('--- boot', i, 'lines', len(b)-1); [print('   ', x[:220]) for x in b[1:14]]
PY
umount $R
echo INSPECT_DONE
