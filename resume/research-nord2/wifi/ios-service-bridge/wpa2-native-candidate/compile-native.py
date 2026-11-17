#!/usr/bin/env python3
"""Recorded generic ARMv8.2 kernel compilation; no CI or image/device operations."""
from pathlib import Path
import argparse,tempfile,json,shutil,subprocess,hashlib
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'shim/nord2-xnu').exists())
a=argparse.ArgumentParser();a.add_argument('--canonical',action='store_true',help='Compile actual generated bsd/dev sources, no private overlay');a.add_argument('--output',type=Path,default=P/'verification/native');args=a.parse_args();V=args.output.resolve();V.mkdir(parents=True,exist_ok=True)
rows=json.loads((R/'research/nord2/wifi-rx-lifecycle/ios/focused/commands.json').read_text());owner=next(x for x in rows if x['name']=='sweet_touch');pure=next(x for x in rows if x['name']=='wifi_default_feature');native=Path(owner['argv'][owner['argv'].index('-c')+1]);W=P.parent/'station-transport-live-candidate';C=P.parent/'connection-bridge/v3/proposed/shim/nord2-xnu'
modules=['wifi_wpa2','wifi_wpa_crypto','wifi_station_transaction','wifi_wpa_owner','wifi_wpa_nst','wifi_station_transport','wifi_station_wire']
records=[]
with tempfile.TemporaryDirectory(prefix='wpa-native-') as td:
 if args.canonical:d=native.parent
 else:
  tree=Path(td)/'xnu';d=tree/'bsd/dev';d.mkdir(parents=True);(tree/'libkern/crypto').mkdir(parents=True);(tree/'libkern/crypto/sweet_bearssl').symlink_to(native.parents[2]/'libkern/crypto/sweet_bearssl',target_is_directory=True)
  shutil.copy2(native,d/'sweet_touch.c')
  for folder in [R/'shim/nord2-xnu',C,W,P,P.parent/'ifnet-native-candidate']:
   for f in folder.iterdir():
    if f.suffix in ('.h','.inc','.c'):shutil.copy2(f,d/f.name)
  f=d/'wifi_normal_backend_forward.inc';s=f.read_text();s='#include "wifi_station_transport_forward.inc"\n'+s;s=s.replace('ncb_rx_active(n)','(ncb_rx_active(n)||nst_native_rx_active(n))').replace('ncb_rx_memory(n,a,b,w)','(ncb_rx_memory(n,a,b,w)||nst_native_rx_memory(n,a,b,w))').replace('ncb_rx_write(n,a,v)','(ncb_rx_write(n,a,v)||nst_native_rx_write(n,a,v))');f.write_text(s)
  f=d/'wifi_basic_config_bind.inc';f.write_text(f.read_text().replace('#define NCT_OWNER_WINDOW(n) nct_native_owner_window(n)','#define NCT_OWNER_WINDOW(n) (nct_native_owner_window(n)||nst_native_owner_window(n))'))
 for name in ['sweet_touch']+modules:
  row=owner if name=='sweet_touch' else pure;cmd=row['argv'].copy();source=d/(name+'.c');cmd[cmd.index('-c')+1]=str(source);cmd[cmd.index('-o')+1]=str(V/(name+'.o'))
  result=subprocess.run(cmd,cwd=row.get('cwd',str(R)),text=True,capture_output=True);(V/(name+'.log')).write_text(result.stdout+result.stderr)
  records.append({'name':name,'source':str(source),'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'argv':cmd,'exit':result.returncode});print(name,result.returncode)
  if result.returncode:print(result.stderr[-4000:]);break
(V/'report.json').write_text(json.dumps({'canonical_generated':args.canonical,'qualification':'Compilation only, no linked/runtime/hardware claim','commands':records},indent=2)+'\n');assert len(records)==8 and all(not x['exit'] for x in records)
