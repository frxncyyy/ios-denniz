#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 exe=Path(d)/'test';subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'pipe.c'),str(p/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
print('PASS: immutable copied-ring / conservative space-plan fixtures')
