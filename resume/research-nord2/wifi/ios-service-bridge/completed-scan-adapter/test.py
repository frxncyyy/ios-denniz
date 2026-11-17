from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;r=p
while not (r/'shim/nord2-xnu').is_dir():r=r.parent
s=p.parent
files=[p/'snapshot.c',p/'test.c',s/'scan-backend-boundary/backend_bss.c',s/'scan-and-initialization/scan_contract.c',s/'initial-query-audit/cache_cursor.c',s/'passive-scan-contract/v2/mapping.c']
with tempfile.TemporaryDirectory() as d:
 e=Path(d)/'test';subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(r/'shim/nord2-xnu')]+list(map(str,files))+['-o',str(e)],check=True);subprocess.run([str(e)],check=True)
