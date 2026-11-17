#!/usr/bin/env python3
from pathlib import Path
import importlib.util,json
P=Path(__file__).resolve().parent;s=importlib.util.spec_from_file_location('connection_parser',P/'parse-connection.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
positive=negative=0

def row(kind,d):return 'NORD2 WIFI '+kind+': '+' '.join(str(k)+'='+str(v)for k,v in d.items())+'\n'
def station(**kw):
 d={k:0 for k in m.STATION};d.update(generation=1,ticket=1,service=5,transaction=8,action=5,associated=1,t_ms=100);d.update(kw);return d
def ready(**kw):
 d=station(service=6,result=1,linked=1,transaction=15,action=0,outcome=1,associated=1,sta=1,ptk=1,gtk=1,port=1,t_ms=300);d.update(kw);return d
def event(**kw):
 d=dict(event=9,published=1,notified=1,status=0,producer=48,consumer=0,generation=1,ticket=1,epoch=2,t_ms=301);d.update(kw);return d
def notice(**kw):
 d=dict(notice=1,current=1,phase=6,result=1,authorized=1,generation=1,ticket=1,epoch=2,t_ms=302);d.update(kw);return d
def link(**kw):
 d=dict(generation=1,authorized=1,changed=1,error=0);d.update(kw);return d
def complete(r=None,e=None,n=None,l=None):return row('IFNET LINK',link()if l is None else l)+row('IOS EVENT',event()if e is None else e)+row('IOS JOINNOTICE',notice()if n is None else n)+row('IOS STATION',ready()if r is None else r)
def yes(text,claim=None,value=True):
 global positive
 r=m.parse(text.encode()if isinstance(text,str)else text);assert r['valid'],r
 if claim:assert r[claim]is value,r
 for k in ('dhcp_exchange_proven','ip_acquisition_proven','internet_connectivity_proven'):assert r[k]is False
 positive+=1;return r
def no(text):
 global negative
 r=m.parse(text.encode()if isinstance(text,str)else text);assert not r['valid'],r
 for k in ('association_observed','native_authorized_observed','authorized_link_observed','current_authorized_link_observed','join_notification_observed','data_air_tx_success_observed','bsd_input_success_observed','ip_acquisition_proven'):assert not r[k],r
 negative+=1
r=yes('');assert r['status']=='NO_NATIVE_JOIN_RECEIPT'
yes(row('IOS STATION',station()),'association_observed');yes(row('IOS STATION',station()),'native_authorized_observed',False)
r=yes(complete(),'authorized_link_observed');assert r['current_authorized_link_observed']and r['join_notification_observed']and not r['data_air_tx_success_observed']and not r['bsd_input_success_observed']
yes(complete(ready(retired=1)),'current_authorized_link_observed') #cookie retirement alone is not disconnect
r=yes(complete()+row('IOS STATION',ready(tx_accept=3,tx_result=2,tx_ok=1,tx_fail=1,rx_delivered=2,t_ms=400)));assert r['data_air_tx_success_observed']and r['bsd_input_success_observed']
last=station(service=7,result=2,transaction=16,outcome=3,linked=0,associated=1,sta=1,ptk=1,gtk=1,port=0,t_ms=500)
r=yes(complete()+row('IFNET LINK',link(authorized=0))+row('IOS STATION',last));assert r['authorized_link_observed']and not r['current_authorized_link_observed']
yes(complete()+row('IOS STATION',station(service=8,result=9,transaction=21,outcome=4,associated=0)),'current_authorized_link_observed',False)
yes(row('IOS STATION',station(service=8,result=2,transaction=0,action=0,associated=0)),'association_observed',False)
yes(complete()+row('IOS LINK',dict(generation=1,up=1,error=5)),'current_authorized_link_observed',False)
guard=dict(reason=1,source=1,stage=1,error=1,valid=0,addr=0,raw=0,mask=0,expected=0)
r=yes(complete()+row('IOS STATIONGUARD',guard),'current_authorized_link_observed',False);assert r['fatal_native_guard_observed']
yes(complete()+row('IOS STATIONGUARD',guard)+row('IOS STATION',ready()),'current_authorized_link_observed',False)
yes(complete(e=event(notified=0,status=0xe00002bc)),'join_notification_observed',False)
yes(complete(l=link(error=5)),'authorized_link_observed',False)
yes(complete(l=link(error=5)),'join_notification_observed',False)
yes(complete(station()),'join_notification_observed',False)
# An old current0 callback may report stale stored metadata, with zero generation.
yes(complete()+row('IOS JOINNOTICE',notice(current=0,generation=0,epoch=1)),'current_authorized_link_observed')
yes(complete()+row('IOS JOINNOTICE',notice(current=0,epoch=1)),'current_authorized_link_observed')
# Missing trace identity does not manufacture an attribution across clients.
event_unbound={k:event()[k]for k in m.EVENT};r=yes(complete(e=event_unbound),'join_notification_observed',False);assert r['uncorrelated_station_events']==1
for omission in ('IFNET LINK','IOS EVENT','IOS JOINNOTICE','IOS STATION'):
 text=''.join(x for x in complete().splitlines(keepends=True)if not x.startswith('NORD2 WIFI '+omission+':'))
 r=yes(text);assert not(r['authorized_link_observed']and r['join_notification_observed'])
# No PMK-profile admission or accepted enqueue alone becomes association.
req=dict(bytes=592,reason=0,key_kind=6,key_bytes=32,native_flags=4,assoc_flags=0,assoc_reason=2,cached_pmk_bytes=0,blacklist_count=0,scope_id=0,conditional_option=0,transition_disable=0,band_preference=0,t_ms=50)
yes(row('IOS JOINREQ',req)+row('IOS JOINCONTROL',dict(request=20,bytes=592,status=0,t_ms=51)),'authorized_link_observed',False)
for key in ('key_kind','key_bytes','native_flags','assoc_flags','assoc_reason','cached_pmk_bytes','blacklist_count','scope_id','conditional_option','transition_disable','band_preference'):
 q=req.copy();q[key]=99;no(row('IOS JOINREQ',q))
q=req.copy();q.update(reason=5,assoc_flags=2048,band_preference=99);yes(row('IOS JOINREQ',q),'association_observed',False)
for key in m.STATION:
 q=ready();q[key]=m.U64+1 if key in('generation','ticket','t_ms')else m.U32+1;no(complete(q))
for key in ('linked','retired','owner_halt','halt','air_pending','associated','sta','ptk','gtk','port'):
 q=ready();q[key]=2;no(complete(q))
for key in ('associated','sta','ptk','gtk','port'):
 q=ready();q[key]=0;no(complete(q))
for key,value in [('generation',0),('ticket',0),('service',9),('result',4),('transaction',23),('action',15),('outcome',6),('native_phase',10),('event',15),('cid',256),('sequence',256),('error',1),('owner_halt',1),('halt',1)]:
 q=ready();q[key]=value;no(complete(q))
no(complete(ready(tx_result=1)))
no(complete(ready(tx_accept=10)))
yes(complete(ready(tx_accept=9)),'authorized_link_observed')
yes(complete(ready(tx_accept=1,queued_drop=1)),'authorized_link_observed')
# Counter width is source uint32: modulo sum is valid, no fake monotonic constraint.
yes(complete(ready(tx_accept=0,tx_result=m.U32,tx_ok=m.U32-1,tx_fail=1,rx_delivered=m.U32)),'data_air_tx_success_observed')
for key,value in [('epoch',3),('generation',0),('ticket',0),('notified',2),('published',0),('producer',4096),('consumer',4096),('producer',1),('event',99)]:
 q=event();q[key]=value;no(complete(e=q))
no(complete()+row('IOS JOINNOTICE',notice(epoch=3)))
no(complete(n=notice(authorized=0)))
for err in (-2147483649,2147483648):no(complete()+row('IOS LINK',dict(generation=1,up=0,error=err)))
no(complete().replace(' transaction=15',' transaction=-1'))
no(complete().replace(' associated=1',' associated=1 associated=1'))
no(complete().replace('IOS STATION:','IOS STATIONUNKNOWN:'))
no(complete()+'NORD2 WIFI IOS STATION: generation=1')
no(complete()+'NORD2 WIFI IOS STATION: junk=1\n')
no(complete().replace('ticket=1 service=6','service=6 ticket=1'))
no(complete().replace('ticket=1 service=6','ticket=1 service=6 extraneous=1'))
no(complete().replace('NORD2 WIFI IOS STATION:','NORD2 WIFI IOS STATION: NORD2 WIFI IOS STATION:'))
no(b'\xff'+complete().encode());no(b'x'*(m.LIMIT+1))
no(row('IOS STATION',ready())*113)
no(complete()+row('IFNET LINK',link())*32)
# Presence of arbitrary IP/DHCP text never provides structured network proof.
yes(complete()+'DHCP ACK 192.0.2.1 Internet ready\n','ip_acquisition_proven',False)
# Fresh stage summary preserves legacy scan observations and delegates structured
# connection rows; implementation readiness is not inferred from either layer.
ss=importlib.util.spec_from_file_location('service_summary',P/'parse-ios-service.py');summary=importlib.util.module_from_spec(ss);ss.loader.exec_module(summary)
scan='NORD2 WIFI IOS EVENT: event=10 published=1 notified=1 status=0 producer=52 consumer=0 t_ms=1\nNORD2 WIFI IOS RESULT: status=1 next=1 count=1 generation=1\nNORD2 WIFI IOS RESULT: status=0 next=1 count=1 generation=1\n'
r=summary.parse(scan+complete());assert r['notified_scan_events']==1 and r['result_rows_encoded']==1 and r['result_exhaustion_observed'];assert r['connection_observations']['authorized_link_observed'];assert not r['connection_observations']['ip_acquisition_proven'];assert 'association_implemented'not in r and 'networking_implemented'not in r;positive+=1
r=summary.parse('',b'\xff');assert not r['connection_observations']['valid'];negative+=1
# Fatal owner logs after synchronous close but before the old service6 snapshot
# is refreshed; linked0/readiness0 cannot count as authorized.
r=yes(complete()+row('IOS STATION',ready(linked=0,port=0))+row('IOS STATIONGUARD',guard),'current_authorized_link_observed',False)
# Last-attempt result1 may remain briefly while synchronous disconnect publishes
# LEAVING and clears actual authorization flags; result alone grants nothing.
yes(complete()+row('IOS JOINNOTICE',notice(notice=3,phase=7,result=1,authorized=0)),'current_authorized_link_observed',False)
yes(row('IOS STATION',dict(last,result=1)),'native_authorized_observed',False)
# One unreported accepted frame after terminal port closure is source-valid.
r=yes(complete()+row('IOS STATION',dict(last,tx_accept=1)));assert not r['current_authorized_link_observed']
report={'status' :'PASS_SYNTHETIC_ONLY','positive':positive,'negative':negative,'scope':'Source-shaped numeric rows, not executed native service or hardware. Startup parser remains independent; no IP inference.'}
(P/'connection-parser-test.json').write_text(json.dumps(report,indent=2)+'\n');print(report)
