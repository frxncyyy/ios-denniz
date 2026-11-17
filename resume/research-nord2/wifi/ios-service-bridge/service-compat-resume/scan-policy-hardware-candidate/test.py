from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').is_dir());S=R/'shim/nord2-xnu'
with tempfile.TemporaryDirectory(prefix='niwi-hardware-policy-') as td:
 cmd=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(P/'proposed'),'-I'+str(S),str(P/'test.c'),str(P/'proposed/wifi_ios_policy.c'),str(S/'wifi_ios_rates.c'),str(S/'wifi_ios_mapping.c'),'-o',td+'/test'];subprocess.run(cmd,check=True);subprocess.run([td+'/test'],check=True)
