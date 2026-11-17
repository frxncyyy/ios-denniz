#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;R=P
while not (R/'shim/nord2-xnu/wifi_capability.c').is_file():R=R.parent
with tempfile.TemporaryDirectory() as d:
 e=Path(d)/'test';subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'shim/nord2-xnu'),str(P/'scan_plan.c'),str(P/'test.c'),str(R/'shim/nord2-xnu/wifi_capability.c'),'-o',str(e)],check=True);subprocess.run([str(e)],check=True)
print('PASS: source activation/passive frames/CAP defaults and opaque normal scan-event header')
