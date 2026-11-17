#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,os
p=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='ios-query-cache-') as d:
 exe=d+'/test';sources=[p/'query_contract.c',p/'cache_cursor.c',p/'test-query.c',p.parent/'scan-and-initialization/scan_contract.c']
 subprocess.run(['cc','-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',*[str(s) for s in sources],'-o',exe],check=True)
 subprocess.run([exe],check=True,env={**os.environ,'ASAN_OPTIONS':'halt_on_error=1','UBSAN_OPTIONS':'halt_on_error=1'})
