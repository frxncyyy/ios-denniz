#!/usr/bin/env python3
"""Compile the actual candidate diagnose() against saved-only mock state."""
from pathlib import Path
import subprocess,tempfile
B=Path(__file__).resolve().parent
s=(B/'audio_device.c').read_text()
assert s.count('static void diagnose(')==1
body=s[s.index('static void diagnose('):s.index('\nstatic int start(')]
with tempfile.TemporaryDirectory() as t:
 t=Path(t);(t/'diagnose-under-test.inc').write_text(body+'\n')
 subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(B/'verification/headers'),'-I'+str(t),str(B/'test-saved-diagnose.c'),'-o',str(t/'test')],check=True)
 subprocess.run([str(t/'test')],check=True,cwd=t)
