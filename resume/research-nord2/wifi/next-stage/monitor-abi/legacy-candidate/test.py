#!/usr/bin/env python3
"""Private offline tests; never execute native SMC or access a device."""
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'collector'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DWIFI_EMI_LEGACY_NO_SMC',str(P/'wifi_emi_legacy.c'),str(P/'test-collector.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 print('PASS:128 exact allowlist, full registers,1280 first-failure positions, NOT_CALLED/validity, zero+constant2 are data',flush=True)
for suite in ['test-old-probe.py','test-legacy-probe.py']:
 subprocess.run(['python3',str(P/suite)],check=True)
