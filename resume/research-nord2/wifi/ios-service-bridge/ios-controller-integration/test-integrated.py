from pathlib import Path
import subprocess,tempfile
P=Path(__file__).resolve().parent;B=P.parent;R=next(x for x in P.parents if x.name=='ios-sweet')
files=[P/'wifi_ios_policy.c',B/'consumer-integration/niw_rate_compat.c',B/'bind-transport-policy/binding_contract.c',B/'control_contract.c',B/'event-pipe/pipe.c',B/'lifecycle-scan-adapter/v2/lifecycle_scan.c',B/'completed-scan-adapter-age/age.c',B/'completed-scan-adapter/snapshot.c',B/'scan-backend-boundary/backend_bss.c',B/'scan-and-initialization/scan_contract.c',B/'initial-query-audit/cache_cursor.c',B/'passive-scan-contract/v2/mapping.c',B/'card-capability-contract/v2/card.c']
flags=['-I'+str(B),'-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(R/'shim/nord2-xnu'),'-I'+str(B/'passive-scan-contract/v2')]
with tempfile.TemporaryDirectory(prefix='niwl-integrated-host-') as td:
 objects=[]
 for i,f in enumerate(files):
  o=Path(td)/(str(i)+'.o');subprocess.run(['clang','-std=c11',*flags,'-c',str(f),'-o',str(o)],check=True);objects.append(o)
 for name in ['test-integrated.cpp','test-lifecycle.cpp']:
  # Lifecycle-only harness still needs native queue stubs; integrated file supplies them without its main.
  source=P/name
  if name=='test-lifecycle.cpp':
   stub=(P/'test-integrated.cpp').read_text();stub=stub[stub.index('static nss_completion callback;'):stub.index('static void finish')];source=Path(td)/name;source.write_text((P/name).read_text().replace('#include \"LifecycleShell.cpp\"','#include \"'+str(P/'LifecycleShell.cpp')+'\"') .replace('static task owner_task,other_task;',stub+'\nstatic task owner_task,other_task;'))
  subprocess.run(['clang++','-std=c++17',*flags,'-Wno-unused-function','-Wno-unused-but-set-variable','-Wno-unused-but-set-global','-pthread',str(source),*map(str,objects),'-o',td+'/test'],check=True);subprocess.run([td+'/test'],check=True)
