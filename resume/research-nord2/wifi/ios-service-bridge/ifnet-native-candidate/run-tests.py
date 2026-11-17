from pathlib import Path
import shutil,subprocess,json,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists())
a=P/'host/iokit/Kernel/arm';b=P/'host/bsd/dev';a.mkdir(parents=True,exist_ok=True);b.mkdir(parents=True,exist_ok=True)
for f in (R/'shim/nord2-xnu').glob('*.h'):shutil.copy2(f,b/f.name)
for n in ['wifi_ifnet_link.h','wifi_ifnet_link.inc']:shutil.copy2(P/n,b/n)
for n in ['wifi_ios_registry.cpp','test-registry.cpp','test-registry-xnu.h']:shutil.copy2(P/n,a/n)
args=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(a/'test-registry.cpp'),'-o',str(P/'test')]
subprocess.run(args,check=True)
with (P/'test.log').open('w') as log:subprocess.run([str(P/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=20)
a=P/'native/iokit/Kernel/arm';b=P/'native/bsd/dev'
for n in ['wifi_ifnet_link.h','wifi_ifnet_link.inc']:shutil.copy2(P/n,b/n)
shutil.copy2(P/'wifi_ios_registry.cpp',a/'wifi_ios_registry.cpp')
cmd=json.loads((P/'native-command.json').read_text())
with (P/'native.log').open('w') as log:subprocess.run(cmd['argv'],cwd=cmd['cwd'],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=45)
print('PASS host ASan/UBSan registry/ifnet; actual generic ARM64 registry TU')
