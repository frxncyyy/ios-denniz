from pathlib import Path
import tempfile,subprocess,shutil
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').exists());W=P.parent/'station-transport-live-candidate'
with tempfile.TemporaryDirectory(prefix='station-scan-regression-') as td:
 d=Path(td);S=d/'shim/nord2-xnu';S.mkdir(parents=True);(d/'tools').mkdir()
 for f in (R/'shim/nord2-xnu').iterdir():
  if f.suffix in ('.c','.h','.inc'):(S/f.name).write_bytes(f.read_bytes())
 for folder in [W,P]:
  for f in folder.iterdir():
   if f.suffix in ('.c','.h','.inc'):(S/f.name).write_bytes(f.read_bytes())
 f=S/'wifi_normal_backend_forward.inc';s=f.read_text();s='#include "wifi_station_transport_forward.inc"\n'+s;s=s.replace('ncb_rx_active(n)','(ncb_rx_active(n)||nst_native_rx_active(n))').replace('ncb_rx_memory(n,a,b,w)','(ncb_rx_memory(n,a,b,w)||nst_native_rx_memory(n,a,b,w))').replace('ncb_rx_write(n,a,v)','(ncb_rx_write(n,a,v)||nst_native_rx_write(n,a,v))');f.write_text(s)
 f=S/'wifi_basic_config_bind.inc';f.write_text(f.read_text().replace('#define NCT_OWNER_WINDOW(n) nct_native_owner_window(n)','#define NCT_OWNER_WINDOW(n) (nct_native_owner_window(n)||nst_native_owner_window(n))'))
 shutil.copy2(R/'tools/test-nord2-wifi-scan-service.py',d/'tools/test-nord2-wifi-scan-service.py');(d/'tools/fixtures').symlink_to(R/'tools/fixtures',target_is_directory=True)
 cal=d/'research/nord2/wifi/calibration';cal.mkdir(parents=True);(cal/'WIFI').symlink_to(R/'research/nord2/wifi/calibration/WIFI')
 out=P/'verification/scan-regression';result=subprocess.run(['python3',str(d/'tools/test-nord2-wifi-scan-service.py'),'--log-dir',str(out)],text=True,capture_output=True);print(result.stdout);print(result.stderr[-2500:]);assert not result.returncode
