from pathlib import Path
import tempfile,json,subprocess,shutil,hashlib
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'shim/nord2-xnu').exists());W=P.parent/'station-transport-live-candidate';C=P.parent/'connection-bridge/v2/proposed/shim/nord2-xnu'
row=next(x for x in json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text()) if x['name']=='sweet_touch');base=row['argv'];source=Path(base[base.index('-c')+1]);V=P/'verification';V.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='wpa-owner-compile-') as td:
 d=Path(td);shutil.copy2(source,d/'sweet_touch.c')
 for folder in [R/'shim/nord2-xnu',C,W,P,P.parent/'ifnet-native-candidate']:
  for f in folder.iterdir():
   if f.suffix in ('.h','.inc'):shutil.copy2(f,d/f.name)
 f=d/'wifi_normal_backend_forward.inc';s=f.read_text();s='#include "wifi_station_transport_forward.inc"\n'+s;s=s.replace('ncb_rx_active(n)','(ncb_rx_active(n)||nst_native_rx_active(n))').replace('ncb_rx_memory(n,a,b,w)','(ncb_rx_memory(n,a,b,w)||nst_native_rx_memory(n,a,b,w))').replace('ncb_rx_write(n,a,v)','(ncb_rx_write(n,a,v)||nst_native_rx_write(n,a,v))');f.write_text(s)
 f=d/'wifi_basic_config_bind.inc';s=f.read_text().replace('#define NCT_OWNER_WINDOW(n) nct_native_owner_window(n)','#define NCT_OWNER_WINDOW(n) (nct_native_owner_window(n)||nst_native_owner_window(n))');f.write_text(s)
 a=base.copy();a[a.index('-c')+1]=str(d/'sweet_touch.c');a[a.index('-o')+1]=str(V/'owner.arm64.o')
 result=subprocess.run(a,cwd=row['cwd'],text=True,capture_output=True);(V/'owner.compile.log').write_text(result.stdout+result.stderr);print(result.returncode);print(result.stderr[-4000:] if result.returncode else '')
 (V/'owner-compile.json').write_text(json.dumps({'argv':a,'exit':result.returncode,'baseline_source':str(source),'baseline_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'qualification':'Actual recorded normal-context flags, private sibling source/header overlay; compile only, no native runtime claim'},indent=2)+'\n')
 assert not result.returncode
