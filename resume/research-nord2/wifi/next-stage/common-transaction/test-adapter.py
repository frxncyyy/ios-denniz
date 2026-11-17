#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
s=(p/'sweet_touch.candidate.c').read_text()
# Exact actual candidate integration and fail-closed rescue contract.
assert s.index('ncc_fresh_owned = true;')<s.index('nw.saved = true;')
assert s.index('ncc_fresh_owned = true;')>s.index('power probe refused PMIC slot=')
assert s.index('ncc_fresh_owned = true;')>s.index('(nw.pwr & 0x1f) != 0x12')
assert '!ni_pmic_read(0x1c20, &mon13) || mon13' in s
assert '!ni_pmic_read(0x1c32, &mon18) || mon18' in s
assert 'cfg_id = ni_rd(conn, 4);\n\tncc_power_window(host, conn, id);' in s
assert 'nw_restore(void)\n{\n\tif (ncc_latched_dirty) { return false; }' in s
assert 'nord2_input_prepare_return(void)\n{\n\tif (ncc_latched_dirty) { return -1; }' in s
assert s.index('ncc_power_window(host, conn, id);')<s.index('\n\tsuccess = true;\nout:')
active=(p.parents[4]/'research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c').read_text()
assert 'ncc_power_window' not in active
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(p),str(p/'test-adapter.c'),str(p/'common.c'),str(p.parent/'cold-snapshot/snapshot.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS exact private candidate placement/fresh-origin/dirty nw_restore+Linuxreturn refusal; active source untouched')
