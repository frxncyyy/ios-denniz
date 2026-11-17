from pathlib import Path
D=Path(__file__).resolve().parent;B=D/'baseline/shim/nord2-xnu';P=D/'proposed/shim/nord2-xnu'
s=(B/'wifi_station_service_bind.inc').read_text();needle='} njs_native;';s=s.replace(needle,needle+'''
/* One copied next attempt; old callback/firmware ownership stays intact until
 * retirement plus the existing real all-queue/fresh-scan barrier. */
static struct {unsigned state,canceled;uint64_t admitted,deadline,ticket,epoch;
 void*cookie;njs_completion completion;struct niwj_request request;struct njs_snapshot snapshot;}njs_handover;
''')
a=s.index(' njs_native.admitted_us=now_us;');b=s.index('\nint nord2_wifi_join_cancel',a);init=s[a:b];init=init[:init.index('\n}')];init=init.replace('njs_native.ticket=++njs_native.serial','njs_native.ticket=next_ticket').replace(' *ticket=njs_native.ticket;IOLockUnlock(nss_native.lock);thread_wakeup((event_t)&nss_native);return 0;','')
helper='''static void njs_accept_locked(const struct niwj_request*q,void*cookie,uint64_t epoch,njs_completion completion,uint64_t next_ticket,uint64_t now_us){
'''+init+'''\n}
'''
pos=s.index('int nord2_wifi_join_enqueue');s=s[:pos]+helper+s[pos:];a=s.index(' if(!nss_native.powered',s.index('int nord2_wifi_join_enqueue'));b=s.index('\nint nord2_wifi_join_cancel',a)
s=s[:a]+''' if(!nss_native.powered||!njs_native.notice_lock||nss_native.failed||nst_native.state.halt||nst_native.state.error||nsr_native.bad||njs_native.serial>=255){IOLockUnlock(nss_native.lock);return 1;}
 if(njs_handover.state){IOLockUnlock(nss_native.lock);return 2;}
 if(njs_native.used&&!njs_rejoin_ready_locked()){
  /* Only an explicit old leave or completed teardown awaiting its real scan
   * permits handover. Never replace a healthy link or overwrite an attempt. */
  if(!njs_native.leave&&!(njs_native.scan_returned&&!njs_native.active&&!njs_native.queued)){IOLockUnlock(nss_native.lock);return 1;}
  njs_handover.state=1;njs_handover.admitted=now_us;njs_handover.deadline=(now_us/1000+8000)*1000;
  njs_handover.ticket=++njs_native.serial;njs_handover.epoch=epoch;njs_handover.cookie=cookie;njs_handover.completion=completion;njs_handover.request=*q;
  njs_handover.snapshot=(struct njs_snapshot){.native_generation=njs_handover.ticket,.client_epoch=epoch,.ticket=njs_handover.ticket,.phase=NJS_QUEUED,.security=q->security};
  *ticket=njs_handover.ticket;IOLockUnlock(nss_native.lock);thread_wakeup((event_t)&nss_native);return 0;
 }
 unsigned busy=njs_native.active||njs_native.queued||njs_native.retiring||njs_native.completion;
 if(busy){IOLockUnlock(nss_native.lock);return 2;}
 njs_accept_locked(q,cookie,epoch,completion,++njs_native.serial,now_us);
 *ticket=njs_native.ticket;IOLockUnlock(nss_native.lock);thread_wakeup((event_t)&nss_native);return 0;
}
'''+s[b:]
s=s.replace('int r=1;if(ticket==njs_native.ticket','int r=1;if(njs_handover.state==1&&ticket==njs_handover.ticket){njs_handover.canceled=1;r=0;}else if(ticket==njs_native.ticket')
s=s.replace(' int r=1;unsigned changed=0;uint64_t generation=0;',' int r=1;unsigned changed=0;uint64_t generation=0;if(njs_handover.state==1){njs_handover.canceled=1;r=0;}')
s=s.replace('*out=njs_native.snapshot;','*out=njs_handover.state?njs_handover.snapshot:njs_native.snapshot;')
pos=s.index('static int njs_owner_work(void){');helper='''/* Worker only; callback delivery never borrows either native lock. */
static int njs_handover_work(void){
 IOLockLock(nss_native.lock);if(njs_handover.state!=1){IOLockUnlock(nss_native.lock);return 0;}
 uint64_t now=njs_uptime_us();unsigned failed=njs_handover.canceled||!nss_native.powered||nss_native.failed||nst_native.state.halt||nst_native.state.error||nsr_native.bad||now<njs_handover.admitted||now>=njs_handover.deadline;
 if(failed){
  njs_handover.state=2;njs_handover.snapshot.phase=NJS_FAILED;njs_handover.snapshot.result=!njs_handover.canceled&&nss_native.powered&&now>=njs_handover.deadline?9:2;
  niwj_clear(&njs_handover.request,sizeof njs_handover.request);
  njs_completion fn=njs_handover.completion;void*cookie=njs_handover.cookie;uint64_t epoch=njs_handover.epoch,ticket=njs_handover.ticket;
  IOLockUnlock(nss_native.lock);fn(cookie,epoch,ticket,NJS_JOIN_FAILED);fn(cookie,epoch,ticket,NJS_RETIRED);
  IOLockLock(nss_native.lock);niwj_clear(&njs_handover,sizeof njs_handover);IOLockUnlock(nss_native.lock);return 1;
 }
 if(!njs_native.active&&!njs_native.queued&&!njs_native.retiring&&!njs_native.completion&&njs_rejoin_ready_locked()){
  njs_accept_locked(&njs_handover.request,njs_handover.cookie,njs_handover.epoch,njs_handover.completion,njs_handover.ticket,njs_handover.admitted);
  niwj_clear(&njs_handover,sizeof njs_handover);
 }
 IOLockUnlock(nss_native.lock);return 0;
}
''';s=s[:pos]+helper+s[pos:];s=s.replace('static int njs_owner_work(void){\n','static int njs_owner_work(void){\n if(njs_handover_work())return 1;\n');(P/'wifi_station_service_bind.inc').write_text(s)
s=(B/'wifi_ios_client.cpp').read_text();s=s.replace('uint64_t joinTicket,joinEpoch,powerEpoch','uint64_t joinTicket,joinEpoch,retiringJoinTicket,retiringJoinEpoch,powerEpoch');s=s.replace('joinTicket=joinEpoch=powerEpoch','joinTicket=joinEpoch=retiringJoinTicket=retiringJoinEpoch=powerEpoch');s=s.replace('ticket=pendingTicket,jt=joinTicket;','ticket=pendingTicket,jt=joinTicket,oldjt=retiringJoinTicket;');s=s.replace('if(jt)nord2_wifi_join_cancel(jt);','if(jt)nord2_wifi_join_cancel(jt);if(oldjt)nord2_wifi_join_cancel(oldjt);')
s=s.replace('else{int x=nord2_wifi_join_disconnect();result=!x?kIOReturnSuccess:x==2?kNiwScanBusy:kIOReturnNotReady;}','''else{int x=nord2_wifi_join_disconnect();result=!x?kIOReturnSuccess:x==2?kNiwScanBusy:kIOReturnNotReady;
     if(!x&&joinTicket&&!retiringJoinTicket){retiringJoinTicket=joinTicket;retiringJoinEpoch=joinEpoch;joinTicket=joinEpoch=0;}
    }''')
s=s.replace('cancel=0,cancelJoin=0;','cancel=0,cancelJoin=0,cancelOldJoin=0;').replace('cancel=pendingTicket;cancelJoin=joinTicket;','cancel=pendingTicket;cancelJoin=joinTicket;cancelOldJoin=retiringJoinTicket;').replace('if(cancelJoin&&result==kIOReturnSuccess)nord2_wifi_join_cancel(cancelJoin);','if(cancelJoin&&result==kIOReturnSuccess)nord2_wifi_join_cancel(cancelJoin);\n if(cancelOldJoin&&result==kIOReturnSuccess)nord2_wifi_join_cancel(cancelOldJoin);')
s=s.replace('bool owned=c->joinTicket==ticket&&c->joinEpoch==epoch;\n bool current=owned&&c->phase==Armed&&c->generation==epoch;','''bool oldOwned=c->retiringJoinTicket==ticket&&c->retiringJoinEpoch==epoch;
 bool owned=(c->joinTicket==ticket&&c->joinEpoch==epoch)||oldOwned;
 bool current=owned&&(!oldOwned||!c->joinTicket)&&c->phase==Armed&&c->generation==epoch;''')
s=s.replace('if(owned&&notice==NJS_RETIRED){c->joinTicket=0;c->joinEpoch=0;retire=true;}','if(owned&&notice==NJS_RETIRED){if(oldOwned)c->retiringJoinTicket=c->retiringJoinEpoch=0;else c->joinTicket=c->joinEpoch=0;retire=true;}');(P/'wifi_ios_client.cpp').write_text(s)
