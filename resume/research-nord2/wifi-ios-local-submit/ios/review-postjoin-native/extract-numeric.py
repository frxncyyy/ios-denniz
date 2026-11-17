#!/usr/bin/env python3
"""Bounded immutable-log numeric extraction only; no device or payload access."""
from pathlib import Path
import collections,hashlib,json,re,subprocess
P=Path(__file__).resolve().parent;R=next(x for x in P.parents if (x/'AGENTS.md').is_file());L=R/'research/nord2/wifi-ios-local-submit/ios/hardware-review/postjoin-spontaneous-close.log'
b=L.read_bytes();assert len(b)==445163 and hashlib.sha256(b).hexdigest()=='bcd722dca6cefc34fe321895070b2868c3f14fc030726dd6853312bca0ac7f9d'
rows=[];samples={};diag=[]
for no,line in enumerate(b.decode(errors='replace').splitlines(),1):
 for tag in ('STATIONPART','NSTDIAG'):
  token='NORD2 WIFI '+('IOS ' if tag=='STATIONPART' else '')+tag+': '
  if token not in line:continue
  numeric=line.split(token,1)[1];assert re.fullmatch(r'(?:[a-z_]+=[0-9]+)(?: [a-z_]+=[0-9]+)*',numeric)
  fields={k:int(v) for k,v in re.findall(r'([a-z_]+)=([0-9]+)',numeric)};rows.append(dict(line=no,tag=tag,fields=fields));assert len(rows)<=400
  if tag=='STATIONPART':n=fields['sample'];samples.setdefault(n,{}).update({k:v for k,v in fields.items() if k not in ('sample','part')})
  else:diag.append(fields)
first=next((n,s) for n,s in samples.items() if s.get('service')==6 and s.get('port')==1);closed=next((n,s) for n,s in samples.items() if n>first[0] and s.get('service')==7 and not s.get('port'))
statuses=collections.Counter((x['d'],x['e'],x['f'],x['h']) for x in diag if x['k']==4)
summary={'raw_log':str(L.relative_to(R)),'raw_bytes':len(b),'raw_sha256':hashlib.sha256(b).hexdigest(),'first_authorized_sample':dict(sample=first[0],**first[1]),'first_close_sample':dict(sample=closed[0],**closed[1]),'local_authorized_interval_ms':closed[1]['t_ms']-first[1]['t_ms'],'observed_txs':[dict(pid=k[0],wlan=k[1],status=k[2],diagnostic_reason=k[3],count=v) for k,v in sorted(statuses.items())],'public_eapol':[x for x in diag if x['k']==5],'observed_publish_type_counts':dict(collections.Counter(x['a'] for x in diag if x['k']==1)),'last_ordinary_diag_number':max(x['n'] for x in diag if x['k']!=8),'late_security_header_available':False,'exact_eapol_failure_reason_available':False,'source_enum_status3':'TX_RESULT_MPDU_ERROR','all_ordinary_frames_failed_claim':False,'dhcp_or_ip_success_claim':False}
(P/'numeric-evidence.json').write_text(json.dumps({'summary':summary,'rows':rows},indent=2)+'\n')
android=R.parent/'nord2-current-kernel';G='drivers/misc/mediatek/connectivity/wlan/core/gen4m/';commits={R:'38b3e67e4fd9b53b415bf8142ce1c687c58c81d2',android:'04d00c2565764c3922c9b4826b5acff1106e533f'}
plans={R:['shim/nord2-xnu/'+n for n in ('wifi_station_transport.c','wifi_station_transport.h','wifi_station_wire.c','wifi_station_source_layout.h','wifi_station_transport_bind.inc','wifi_wpa_nst.c','wifi_wpa2.c','wifi_station_transaction.c','wifi_station_service_bind.inc','wifi_station_diag.c','wifi_ifnet_link.inc')],android:[G+n for n in ('include/wlan_lib.h','include/nic_cmd_event.h','include/nic/nic_connac2x_tx.h','include/nic/nic_tx.h','include/wsys_cmd_handler_fw.h','nic/nic_txd_v2.c','nic/nic_tx.c','nic/que_mgt.c','os/linux/gl_kal.c','common/wlan_oid.c')]}
pins=[]
for root,paths in plans.items():
 for path in paths:
  f=root/path;data=f.read_bytes();assert data==subprocess.check_output(['git','show',commits[root]+':'+path],cwd=root),path
  pins.append(dict(path=str(f.relative_to(R)) if f.is_relative_to(R) else str(f),bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),commit=commits[root]))
(P/'source-pins.json').write_text(json.dumps(pins,indent=2)+'\n')
print(json.dumps({k:summary[k] for k in ('local_authorized_interval_ms','observed_txs','last_ordinary_diag_number','late_security_header_available')},indent=2))
