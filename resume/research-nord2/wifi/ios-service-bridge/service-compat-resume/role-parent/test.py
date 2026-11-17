#!/usr/bin/env python3
import subprocess,tempfile
from pathlib import Path
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='nord2-ios-scalar-') as tmp:
 exe=Path(tmp)/'test'
 subprocess.run(['clang','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'wifi_ios_scalar.c'),str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS: 15365 request/state/capacity/malformed cases plus 4 null-argument refusals; fail-fast ASan/UBSan')
