from pathlib import Path
import hashlib,json,re
root=Path.cwd();base=root/'research/nord2/wifi/next-stage/main-fw-transport';dst=root/'shim/nord2-xnu';fix=root/'tools/fixtures/nord2-wifi-main'
p=base/'wfdma/v2';m=json.loads((p/'manifest.json').read_text())
assert hashlib.sha256((p/'manifest.json').read_bytes()).hexdigest()=='a10267a1c54f2dbddf25ae564136090ffbe7e928cb5e9a34d168bb2100401ad3'
for n,h in m['files'].items():assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h,n
names=['command','dmashdl','init_plan','initializer','transport']
rename={n+e:'wifi_main_'+n+e for n in names for e in ('.c','.h')}
rename.update({'main_fw_layout.h':'wifi_main_layout.h','main_fw_layout.c':'wifi_main_layout.c','pool_probe.h':'pool_probe.h','wifi_dma_preflight.h':'wifi_dma_preflight.h'})
def norm(s):
 return re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+rename.get(Path(m[1]).name,Path(m[1]).name)+'"',s)
proof=[]
for n in names:
 for e in ('.c','.h'):
  source=p/(n+e);out=dst/rename[n+e];out.write_text(norm(source.read_text()));proof.append({'from':str(source.relative_to(root)),'to':str(out.relative_to(root)),'sha256':hashlib.sha256(out.read_bytes()).hexdigest()})
for e in ('.c','.h'):
 source=base/('main_fw_layout'+e);out=dst/('wifi_main_layout'+e);out.write_text(norm(source.read_text()));proof.append({'from':str(source.relative_to(root)),'to':str(out.relative_to(root)),'sha256':hashlib.sha256(out.read_bytes()).hexdigest()})
for n in ['test-command.c','test-dmashdl.c','test-plan.c','test-initializer.c','test-transport.c']:(fix/n).write_text(norm((p/n).read_text()))
(root/'research/nord2/wifi-main/helper-promotion.json').write_text(json.dumps({'normalization':'Include filenames only; symbol/body behavior preserved','files':proof},indent=2)+'\n')
