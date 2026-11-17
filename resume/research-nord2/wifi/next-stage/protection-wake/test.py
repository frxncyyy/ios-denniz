#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'wake.c'),str(p/'test.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
