from pathlib import Path
import subprocess,tempfile
p=Path(__file__).resolve().parent;root=p.parents[5]
with tempfile.TemporaryDirectory() as d:
 out=Path(d)/'test'
 subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(p),str(p/'test-native.c'),str(p/'scope.c'),str(p/'strap.c'),str(p.parent.parent/'adie-id/id.c'),str(root/'shim/nord2-xnu/wifi_common.c'),str(root/'shim/nord2-xnu/wifi_cold_snapshot.c'),'-o',str(out)],check=True)
 subprocess.run([str(out)],check=True)
print('PASS combined private gate2000/scopedID lifetime/faulttests withactuale70c500e;no hardware')
