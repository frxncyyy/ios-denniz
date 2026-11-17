from pathlib import Path
import json,hashlib,runpy,collections,re
R=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists());O=Path(__file__).resolve().parent;D=R/'research/nord2/wifi-ios-live/ios';L=D/'hardware-review/after-first-disconnect.log';J=D/'hardware-live/disconnect-first.jsonl'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();raw=L.read_bytes();text=raw.decode();snap=[]
fields='op result generation time_ms service flags native_phase error halt join_ready submitted completed tx_pending rx_pending wire_busy air_pending data_cursor command_cursor data_published data_ddone data_report firmware_retirement_proven same_boot_verified'.split()
for l in J.read_text().splitlines():x=json.loads(l);snap.append({k:x[k] for k in fields})
assert len(snap)==2 and snap[0]['op']==7 and snap[0]['result']==0 and snap[1]['op']==1 and snap[1]['result']==0
last=snap[-1];assert last['generation']==1 and last['join_ready']==1 and last['service']==8 and last['flags']==64 and last['submitted']==last['completed']==91
assert all(not last[k] for k in ('halt','error','tx_pending','rx_pending','wire_busy','air_pending','data_published','data_ddone','data_report'))
conn=runpy.run_path(str(R/'tools/nord2-wifi-connection-log.py'));expanded,framing=conn['expand_station_parts'](text);assert not framing
stations=[]
for l in expanded.splitlines():
 if l.startswith('NORD2 WIFI IOS STATION: '):stations.append({k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',l)})
t=stations[-1];assert t['transaction']==21 and t['peer_step']==3 and t['cid']==23 and t['retired']==1 and t['submitted']==t['dma']==91
assert not any(t[k] for k in ('linked','owner_halt','halt','error','air_pending','associated','sta','ptk','gtk','port'))
connection=conn['parse'](raw);assert connection['issues']==['contradictory authorized station snapshot']*2
transitions=[{k:x[k] for k in ('generation','ticket','transaction','action','outcome','service','linked','result','associated','sta','ptk','gtk','port','t_ms')} for x in stations if x['service']==6 and x['linked'] and not conn['authorized'](x)]
tx=runpy.run_path(str(R/'tools/nord2-wifi-tx-correlation.py'))['parse'](text);samples=tx['samples'];assert tx['valid']
ordinary=[s for s in samples if s['kind']==3];security=[s for s in samples if s['kind']==2];management=[s for s in samples if s['kind']==1]
assert len(ordinary)==63 and all(s['TXSTATUS']['status']==3 and s['TXSTATUS']['count']==30 and s['TXFINAL']['local']==1 for s in ordinary)
assert len(security)==3 and len(management)==3 and all(s['TXSTATUS']['status']==0 for s in security+management)
classes=collections.Counter((s['TXPUB']['ether'],s['TXPUB']['dhcp']) for s in ordinary)
data=runpy.run_path(str(R/'tools/nord2-wifi-data-diag.py'))['parse'](text,require_complete=False);assert data['valid'] and data['complete']
summary=data['attempts'][0]['summary']
scanreturn=re.findall(r'^NORD2 WIFI IOS SCANRETURN: .*$',text,re.M);assert len(scanreturn)==1
assert text.index(scanreturn[0])<text.index('NORD2 WIFI IOS SCAN: ticket=19 generation=1 status=0')
report={'scope':'bounded read-only physical host-lifecycle/TX evidence; no RF/control action','inputs':{str(p.relative_to(R)):{'bytes':p.stat().st_size,'sha256':h(p)} for p in (L,J)},'live_snapshots':snap,'final_station':t,'scan_return':scanreturn[0],'automatic_fresh_scan':{'ticket':19,'client_generation':1,'status':0,'sequence':20,'frames':14,'bss':11,'done':1,'partial':0,'canceled':0,'error':0,'rx_error':0},'physical_host_teardown_ready_observed':True,'second_native_join_observed':False,'firmware_teardown_ack_proven':False,'connection_reader_valid':connection['valid'],'connection_reader_issues':connection['issues'],'unhandled_authorized_rekey_transitions':transitions,'sampled_ordinary':{'count':63,'status':3,'reported_tx_count':30,'source_name':'TX_RESULT_MPDU_ERROR','classification':[{'ether':a,'dhcp':b,'count':n}for (a,b),n in sorted(classes.items())]},'sampled_security':[{k:s[k]for k in ('pid','g','TXPUB','TXSTATUS','TXFINAL')}for s in security],'sampled_management_count':3,'data_summary':summary,'diagnostic_complete_does_not_prove_capture_integrity':True,'shared127_not_packet_attributed':True,'new_proven_descriptor_mismatch':False,'OPEN_native_comparison_supported_by_current_image':False,'DHCP_lease_or_IP_proven':False,'source_pins':{str(p.relative_to(R)):h(p) for p in [R/'shim/nord2-xnu/wifi_station_rejoin.h',R/'shim/nord2-xnu/wifi_station_transport_bind.inc',R/'shim/nord2-xnu/wifi_station_service_bind.inc',R/'shim/nord2-xnu/wifi_scan_return_bind.inc',R/'shim/nord2-xnu/wifi_station_transport.c',R/'shim/nord2-xnu/wifi_station_wire.c',R/'shim/nord2-xnu/wifi_station_tx.c',R/'shim/nord2-xnu/wifi_ios_join.c',R/'shim/nord2-xnu/wifi_live_control_bind.inc']}}
(O/'review.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'host_teardown_ready':True,'ordinary_unique_pid_failures':len(ordinary),'security_unique_pid_successes':len(security),'connection_reader_rekey_gap':True,'review_sha256':h(O/'review.json')}))
