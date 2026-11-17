from pathlib import Path
import hashlib,json,re
R=Path.cwd();B=R/'research/nord2/wifi/next-stage/main-fw-transport';D=R/'shim/nord2-xnu';F=R/'tools/fixtures/nord2-wifi-main'
packages=[(B/'main-fw-native-v2','0856bff4a4a5d11f6dcae3bf867268a6b2b21b6064129d830e1c40cc5355576e','source_files_sha256'),(B/'wfdma/native-candidate','e1bfc6cf9c522c13e78f2e00fd370929639328b8df7f2fd9de2f298251a7b820','files')]
for p,digest,key in packages:
 assert hashlib.sha256((p/'manifest.json').read_bytes()).hexdigest()==digest
 assert json.loads((p/'independent-review.json').read_text())['result'].startswith('PASS')
 for n,h in json.loads((p/'manifest.json').read_text())[key].items():assert hashlib.sha256((p/n).read_bytes()).hexdigest()==h,n
names=['command','dmashdl','init_plan','initializer','transport','fw_place','fw_xnu','phy_backup','phy_exchange','phy_force','phy_xnu','wfdma_native']
ren={n+e:'wifi_main_'+n+e for n in names for e in ('.c','.h')}
ren.update({'main_fw_layout.h':'wifi_main_layout.h','main_halt.h':'wifi_main_halt.h','wfdma_bind.inc':'wifi_main_wfdma_bind.inc','main_bind.inc':'wifi_main_bind.inc'})
def normalize(s):return re.sub(r'#include "([^\"]+)"',lambda m:'#include "'+ren.get(Path(m[1]).name,Path(m[1]).name)+'"',s)
proof=[]
p=B/'main-fw-native-v2'
for n in ['fw_place','fw_xnu','phy_backup','phy_exchange','phy_force','phy_xnu']:
 for e in ('.c','.h'):
  src=p/(n+e);dest=D/ren[n+e];dest.write_text(normalize(src.read_text()));proof.append((str(src.relative_to(R)),str(dest.relative_to(R)),hashlib.sha256(dest.read_bytes()).hexdigest()))
for p,n in [(p,'main_halt.h'),(B/'wfdma/native-candidate','wfdma_native.h'),(B/'wfdma/native-candidate','wfdma_bind.inc'),(B/'wfdma/native-candidate','main_bind.inc')]:
 src=p/n;dest=D/ren[n];dest.write_text(normalize(src.read_text()));proof.append((str(src.relative_to(R)),str(dest.relative_to(R)),hashlib.sha256(dest.read_bytes()).hexdigest()))
for src,name in [(B/'main-fw-native-v2/test-native.c','test-spans.c'),(B/'main-fw-native-v2/test-halt.c','test-halt.c'),(B/'wfdma/native-candidate/test-native.c','test-native.c')]:
 (F/name).write_text(normalize(src.read_text()))
(R/'research/nord2/wifi-main/native-promotion.json').write_text(json.dumps({'normalization':'Include names only','files':proof},indent=2)+'\n')
p=R/'tools/nord2-mkpatch.sh';s=p.read_text();needle='wifi_main_transport wifi_main_layout"';assert s.count(needle)==1;s=s.replace(needle,'wifi_main_transport wifi_main_layout wifi_main_fw_place wifi_main_fw_xnu wifi_main_phy_backup wifi_main_phy_exchange wifi_main_phy_force wifi_main_phy_xnu"')
needle='cp shim/nord2-xnu/pool_bind.inc "$BT/bsd/dev/pool_bind.inc"'
extra='\nfor name in wifi_main_wfdma_native.h wifi_main_halt.h wifi_main_wfdma_bind.inc wifi_main_bind.inc; do\n    cp "shim/nord2-xnu/$name" "$BT/bsd/dev/$name"\n    NEW_FILES="$NEW_FILES bsd/dev/$name"\ndone'
assert s.count(needle)==1;s=s.replace(needle,needle+extra);p.write_text(s)
p=R/'research/ci-39158169212/buildtree/xnu/bsd/conf/files';s=p.read_text()
for n in ['fw_place','fw_xnu','phy_backup','phy_exchange','phy_force','phy_xnu']:
 line='bsd/dev/wifi_main_'+n+'.c\t\t\tstandard\n';assert line not in s;s+=line
p.write_text(s)
p=R/'research/ci-39158169212/buildtree/xnu/bsd/dev/sweet_touch.c';s=p.read_text();needle='#include "pool_bind.inc"';assert s.count(needle)==1;s=s.replace(needle,needle+'\n#include "wifi_main_wfdma_bind.inc"\n#include "wifi_main_bind.inc"')
needle='    nppa_after_rom(); /* exact nord2wifirom=1, prepared before probe */';assert s.count(needle)==1;s=s.replace(needle,needle+'\n    nwfn_after_pool(); /* separate nord2wifimain=1; real firmware/calibration */');p.write_text(s)
