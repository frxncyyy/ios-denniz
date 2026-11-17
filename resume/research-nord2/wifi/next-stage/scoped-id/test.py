#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
root=p.parents[4]
src=root.parent/'nord2-current-kernel/drivers/misc/mediatek/connectivity/conninfra/platform/mt6893'
pos=(src/'mt6893_pos.c').read_text()
assert 'GPIO_BASE_ADDR + GPIO_DIR5_SET, 0x00100000' in pos
assert 'GPIO_BASE_ADDR + GPIO_DOUT5_SET, 0x00100000' in pos
assert 'GPIO_BASE_ADDR + GPIO_MODE22, 0x0, 0x70000' in pos
h=(src/'include/mt6893_consys_reg_offset.h').read_text()
assert 'CONN_SEMAPHORE_M2_OWN_STA' in h and '0x2000' in h
assert 'CONN_SEMAPHORE_M2_OWN_REL' in h and '0x2200' in h
# Candidate stays private and preserves original dirty refusal paths.
s=(p/'sweet_touch.candidate.c').read_text()
assert 'if (ncc_latched_dirty) { return false; }' in s
assert 'if (ncc_latched_dirty) { return -1; }' in s
assert '#include "common_adapter.candidate.inc"' in s
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),str(p/'test-native.c'),str(p/'scope.c'),str(p/'strap.c'),str(p.parent/'adie-id/id.c'),str(root/'shim/nord2-xnu/wifi_common.c'),str(root/'shim/nord2-xnu/wifi_cold_snapshot.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS private nativeintegration, no active/canonical changes')
