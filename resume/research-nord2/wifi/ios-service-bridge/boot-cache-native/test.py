from pathlib import Path
import tempfile,subprocess
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if p.name=='ios-sweet')
with tempfile.TemporaryDirectory(prefix='nse-') as td:
 exe=Path(td)/'test';subprocess.run(['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'shim/nord2-xnu'),str(P/'test.c'),*map(str,[P.parent/'lifecycle-scan-adapter/v2/lifecycle_scan.c',P.parent/'completed-scan-adapter-age/age.c',P.parent/'completed-scan-adapter/snapshot.c',P.parent/'scan-backend-boundary/backend_bss.c',P.parent/'scan-and-initialization/scan_contract.c',P.parent/'initial-query-audit/cache_cursor.c',P.parent/'passive-scan-contract/v2/mapping.c']),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
