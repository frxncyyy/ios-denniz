from pathlib import Path
import tempfile, subprocess
P=Path(__file__).resolve().parent; B=P.parent.parent; R=next(p for p in P.parents if p.name=='ios-sweet')
files=[P/'lifecycle_scan.c',P/'test.c',B/'completed-scan-adapter-age/age.c',B/'completed-scan-adapter/snapshot.c',B/'scan-backend-boundary/backend_bss.c',B/'scan-and-initialization/scan_contract.c',B/'initial-query-audit/cache_cursor.c',B/'passive-scan-contract/v2/mapping.c']
with tempfile.TemporaryDirectory(prefix='nord2-lifecycle-scan-') as td:
 exe=Path(td)/'test';subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'shim/nord2-xnu'),*map(str,files),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
