#!/usr/bin/env python3
"""Read-only bounded source pinning. Writes only next to this script."""
from pathlib import Path
import hashlib,json,subprocess
OUT=Path(__file__).resolve().parent
ROOT=next(p for p in OUT.parents if (p/'AGENTS.md').is_file())
ANDROID=ROOT.parent/'nord2-current-kernel'
A='04d00c2565764c3922c9b4826b5acff1106e533f'
C='1a4b1fed3d36622b9697085c55640dbd5f4595eb'
G='drivers/misc/mediatek/connectivity/wlan/core/gen4m/'
plans={
 'android':(ANDROID,A,{
 G+'os/linux/gl_kal.c':[(2667,2697),(2744,2775),(3020,3065),(4022,4126),(5342,5353)],
 G+'os/linux/include/gl_kal.h':[(910,919)],
 G+'common/wlan_lib.c':[(2334,2348),(2403,2418),(4256,4314),(4352,4413),(4435,4478),(4489,4504),(5533,5578),(10612,10691)],
 G+'nic/nic_tx.c':[(2327,2419),(2765,2805),(2985,3078),(3355,3448),(4363,4386),(4411,4432)],
 G+'nic/nic_txd_v2.c':[(425,450)],
 G+'mgmt/p2p_role_fsm.c':[(3944,3983)],
 G+'mgmt/privacy.c':[(660,724)],
 G+'include/nic/nic_tx.h':[(198,205)]}),
 'canonical':(ROOT,C,{
 'shim/nord2-xnu/wifi_station_transport.c':[(35,62),(76,77),(84,119),(123,130),(184,198)],
 'shim/nord2-xnu/wifi_station_transport.h':[(1,78)],
 'shim/nord2-xnu/wifi_wpa_owner.c':[(8,18),(38,50)],
 'shim/nord2-xnu/wifi_wpa_owner.h':[(1,65)],
 'shim/nord2-xnu/wifi_wpa_nst.c':[(25,35),(51,78)],
 'shim/nord2-xnu/wifi_station_transaction.c':[(87,116),(131,149)],
 'shim/nord2-xnu/wifi_wpa2.c':[(127,183)],
 'shim/nord2-xnu/wifi_wpa2.h':[(25,40)],
 'shim/nord2-xnu/wifi_station_service_bind.inc':[(23,29),(145,155),(210,222)]}),
 'aosp_reference':(OUT,None,{
 'aosp-wpa.c':[(158,170),(1616,1655),(1763,1828),(2638,2718)],
 'aosp-wpa_i.h':[(269,274)],
 'aosp-wpas_glue.c':[(90,130),(465,470),(1430,1438)]})}
records=[];text=[]
for origin,(root,commit,files) in plans.items():
 for name,spans in files.items():
  p=root/name;b=p.read_bytes();sha=hashlib.sha256(b).hexdigest()
  if commit:
   tracked=subprocess.check_output(['git','show',f'{commit}:{name}'],cwd=root)
   assert tracked==b,(origin,name,'source differs from pinned commit')
  records.append(dict(origin=origin,path=str(p.relative_to(ROOT)) if p.is_relative_to(ROOT) else str(p),bytes=len(b),sha256=sha,commit=commit,spans=spans))
  lines=b.decode().splitlines()
  for start,end in spans:
   text.append(f'[{origin}] {name}:{start}-{end} SHA256 {sha}\n'+'\n'.join(f'{i}: {lines[i-1]}' for i in range(start,min(end,len(lines))+1)))
log=ROOT/'research/nord2/wifi-ios-txs/ios/hardware-review/join-first-terminal.log'
b=log.read_bytes();assert len(b)==368040 and hashlib.sha256(b).hexdigest()=='e83cd4e66106cd274cf96ce1370b47504c162c58d4b14464c05772ddbbd3fff7'
records.append(dict(origin='hardware_log_identity_only',path=str(log.relative_to(ROOT)),bytes=len(b),sha256=hashlib.sha256(b).hexdigest()))
(OUT/'evidence.txt').write_text('\n\n'.join(text)+'\n')
(OUT/'source-pins.json').write_text(json.dumps(dict(android_commit=A,canonical_runtime_commit=C,aosp_reference_commit='9251a8681cdf164191f31603e7319eb44dfc1e73',aosp_is_selected_userspace=False,sources=records),indent=2)+'\n')
print('PINNED',len(records),'sources; no runtime or hardware action')
