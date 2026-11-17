#!/usr/bin/env python3
from pathlib import Path
import tempfile,subprocess,os
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='ios-scan-contract-') as d:
 exe=d+'/test';subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'scan_contract.c'),str(p/'test-scan.c'),'-o',exe],check=True)
 subprocess.run([exe],check=True,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
