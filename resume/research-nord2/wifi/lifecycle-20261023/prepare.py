from pathlib import Path
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());O=P/'candidate/shim/nord2-xnu';O.mkdir(parents=True,exist_ok=True)
def read(n):return (R/'shim/nord2-xnu'/n).read_text()
def put(n,s):(O/n).write_text(s)
def rep(s,a,b):
 assert s.count(a)==1,(a,s.count(a));return s.replace(a,b)
s=read('wifi_station_transport.h');s=rep(s,' unsigned cleanup_started;',' unsigned cleanup_started;\n struct nsb_scan *associated_scan;unsigned associated_scan_published;');s=rep(s,'int nst_scan_admissible(','''/* Worker-only borrowed slot remains owned until detach after actual scan completion.
 * Other RX and security/data dispatch keep their normal owner and replay path. */
int nst_associated_scan(struct nst *,struct nsb_scan *,const uint8_t *,size_t);
int nst_associated_scan_detach(struct nst *,struct nsb_scan *);
int nst_scan_admissible(''');put('wifi_station_transport.h',s)
s=read('wifi_station_transport.c');s=rep(s,'case 7:return n==64;','case 3:return n==1188;case 7:return n==64;');s=rep(s,'if(cid==7&&(p[10]', 'if(cid==3&&internal!=3)return -1;\n if(cid==7&&(p[10]')
a='static int frame(struct nst*s';i=s.index(a);s=s[:i]+'''int nst_associated_scan(struct nst*s,struct nsb_scan*scan,const uint8_t*p,size_t n){
 if(!acceptable(s)||!scan||scan->entered||!p||n!=1188||s->associated_scan||s->cleanup_started||s->link_lost||!s->controlled_port||!s->sta_active||!s->ptk_ready||!s->ptk.installed||!s->gtk.installed||s->channel_held||p[1]!=s->peer.bss||p[2]||p[7]!=1)return -1;
 if(busy(s))return 1;
 int r=command(s,UINT64_MAX-1,3,p,n,3);if(!r){s->associated_scan=scan;s->associated_scan_published=0;}return r;
}
int nst_associated_scan_detach(struct nst*s,struct nsb_scan*scan){
 if(!s||!scan||s->associated_scan!=scan||!scan->done||scan->active||(s->wire_busy&&s->wire_cid==3))return -1;
 s->associated_scan=0;s->associated_scan_published=0;return 0;
}
'''+s[i:]
s=rep(s,'s->phase=NST_TX_PUBLISH;s->init->transport.submit_attempted=1;', 's->phase=NST_TX_PUBLISH;s->init->transport.submit_attempted=1;\n  if(s->wire_cid==3&&s->internal_wire==3){if(!s->associated_scan||nsb_begin(s->associated_scan,s->wire[64],s->last,UINT64_C(14000000)))return fail(s,48);s->associated_scan_published=1;}')
s=rep(s,'unsigned type=(u16(p+2)>>11)&31;','''unsigned type=(u16(p+2)>>11)&31;
 if(s->associated_scan&&s->associated_scan->active){
  if(type==7&&(u16(p+2)&0x380f)==0x3801)(void)nsb_add(s->associated_scan,p,n,s->last);
  else if(type==7&&(u16(p+2)&0x380f)==0x3800&&n>=36&&p[28]==0x0d)(void)nsb_complete_event(s->associated_scan,p,n,s->last);
 }''')
# nsb_add on nonbeacon calls expire and doesn't mutate record, no security loosening
s=rep(s,'if(peer_progress(s,fn,ctx))return -1;s->phase=NST_IDLE;', 'if(peer_progress(s,fn,ctx))return -1;\n if(s->associated_scan&&s->associated_scan_published&&!s->associated_scan->done&&(!s->associated_scan->active||nsb_expire(s->associated_scan,s->last)))return fail(s,49);s->phase=NST_IDLE;');put('wifi_station_transport.c',s)
s=read('wifi_scan_service.h');s=rep(s,'unsigned scanning;','unsigned scanning,powered;');s=rep(s,'int nord2_wifi_scan_info(struct nss_info *);','''int nord2_wifi_scan_info(struct nss_info *);
/* Logical radio admission; no physical SoC power-down claim. OFF revokes data
 * synchronously and owner drains scans then existing station/key cleanup. */
int nord2_wifi_power_set(unsigned on);''');put('wifi_scan_service.h',s)
s=read('wifi_scan_service_bind.inc');s=rep(s,'IOLock*lock;unsigned ready,failed;','IOLock*lock;unsigned ready,failed,powered;');s=rep(s,'*out=nss_native.info;out->scanning=0;','*out=nss_native.info;out->powered=nss_native.powered;out->scanning=0;');s=rep(s,'if(njs_scan_blocked_locked()){','if(!nss_native.powered||njs_scan_blocked_locked()){');s=rep(s,' njs_initialize_locked();',' nss_native.powered=1;njs_initialize_locked();');put('wifi_scan_service_bind.inc',s)
s=read('wifi_station_service_bind.inc');s=rep(s,'static int njs_scan_blocked_locked(void){return njs_native.queued||njs_native.active||(njs_native.used&&!njs_native.scan_returned);}', '''static int njs_associated_scan_ready_locked(void){return njs_native.active&&njs_native.linked&&!njs_native.queued&&!njs_native.leave&&!njs_native.canceled&&nwo_authorized(&njs_native.driver.owner)&&nst_native.state.controlled_port&&!nst_native.state.cleanup_started;}
static int njs_scan_blocked_locked(void){return !nss_native.powered||njs_native.queued||(njs_native.active&&!njs_associated_scan_ready_locked())||(njs_native.used&&!njs_native.active&&!njs_native.scan_returned);}''')
s=rep(s,'if(!njs_native.notice_lock||nss_native.failed||','if(!nss_native.powered||!njs_native.notice_lock||nss_native.failed||')
s=rep(s,'if(!nss_native.failed&&njs_native.active&&!njs_native.canceled','if(nss_native.powered&&!nss_native.failed&&njs_native.active&&!njs_native.canceled')
a='/* Compiled native module implements';i=s.index(a);s=s[:i]+'''int nord2_wifi_power_set(unsigned on){
 if(on>1||!nss_available()||!njs_native.notice_lock)return 1;
 IOLockLock(nss_native.lock);if(nss_native.failed){IOLockUnlock(nss_native.lock);return 1;}
 nss_native.powered=on;
 if(!on)for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state==NSS_QUEUED||nss_native.slot[k].state==NSS_RUNNING)nss_native.slot[k].canceled=1;
 IOLockUnlock(nss_native.lock);
 /* Existing API serializes revoke against link-up and invokes BSD outside NSS.
  * Accepted OFF is immediate data admission closure; owner cleanup is async. */
 if(!on){int r=nord2_wifi_join_disconnect();if(r<0)return -1;}
 thread_wakeup((event_t)&nss_native);return 0;
}
/* Only owner calls this under admission lock; never borrows NCT while linked. */
static int njs_associated_scan_start_locked(void){
 if(!nss_native.powered||!njs_associated_scan_ready_locked()||nst_native.state.associated_scan||nst_native.state.wire_busy||nst_native.state.air_pending||nst_native.state.peer_kind)return 0;
 struct nss_slot*slot=0;for(unsigned k=0;k<NSS_SLOTS;k++)if(nss_native.slot[k].state==NSS_QUEUED&&!nss_native.slot[k].canceled&&(!slot||nss_native.slot[k].ticket<slot->ticket))slot=&nss_native.slot[k];if(!slot)return 0;
 const struct ncs_channel*channels=slot->request.count?slot->request.channels:nss_native.info.channels;size_t count=slot->request.count?slot->request.count:nss_native.info.count;
 uint8_t sequence=(uint8_t)(nss_native.scan_sequence+1);
 if(ncs_build(ncd_native.diag.frame,ncd_native.diag.frame_bytes,ncd_native.diag.history[ncd_native.diag.events-1].sequence,nss_native.info.mac,channels,count,1,2,sequence,1,slot->request.dwell_tu,0,&nss_native.frames))return -1;
 int r=nst_associated_scan(&nst_native.state,&slot->scan,nss_native.frames.scan+64,NCS_SCAN_BYTES-64);if(r)return r<0?-1:0;
 nss_native.scan_sequence=sequence;slot->state=NSS_RUNNING;return 0;
}
static struct nss_slot*njs_associated_scan_done_locked(void){
 struct nsb_scan*scan=nst_native.state.associated_scan;if(!scan||!scan->done||scan->active||(nst_native.state.wire_busy&&nst_native.state.wire_cid==3))return 0;
 for(unsigned k=0;k<NSS_SLOTS;k++)if(&nss_native.slot[k].scan==scan){if(nst_associated_scan_detach(&nst_native.state,scan))return 0;nss_native.slot[k].captured=scan->last;return &nss_native.slot[k];}return 0;
}
'''+s[i:]
s=rep(s,'if(njs_native.canceled||njs_native.leave){(void)nwn_cancel','if((njs_native.canceled||njs_native.leave)&&!nst_native.state.associated_scan){(void)nwn_cancel')
s=rep(s,' int result=nwn_pump(', ' if(njs_associated_scan_start_locked()<0){IOLockUnlock(nss_native.lock);return njs_fatal_owner();}\n int result=nwn_pump(')
s=rep(s,' njs_snapshot_locked();njs_trace_locked(0);',' struct nss_slot*scan_done=njs_associated_scan_done_locked();\n njs_snapshot_locked();njs_trace_locked(0);')
s=rep(s,' if(link_down){IOLockLock(', ''' if(scan_done)nss_deliver(scan_done,scan_done->scan.completion.state==7&&!scan_done->scan.partial&&!scan_done->scan.dropped?NSS_COMPLETE:NSS_PARTIAL);
 /* Canceled queued slots must retire even while station owns the loop. */
 for(unsigned k=0;k<NSS_SLOTS;k++){IOLockLock(nss_native.lock);unsigned cancel_scan=nss_native.slot[k].state==NSS_QUEUED&&nss_native.slot[k].canceled;IOLockUnlock(nss_native.lock);if(cancel_scan)nss_deliver(&nss_native.slot[k],NSS_CANCELED);}
 if(link_down){IOLockLock(''')
s=rep(s,'if(e->kind==NST_COMMAND_DMA&&e->cid==7','if(!nss_native.powered&&e->kind==NST_EAPOL)return 0;\n if(e->kind==NST_COMMAND_DMA&&e->cid==7');put('wifi_station_service_bind.inc',s)
s=read('wifi_ios_client.cpp');s=rep(s,'if(unsupportedSet==19&&!a->structureOutputSize&&niw_u32((const uint8_t*)carrier+4)==24&&!niw_u32((const uint8_t*)carrier+tail+20)&&!niwi_power_on_admit(power,24)){','if(unsupportedSet==19&&!a->structureOutputSize&&niw_u32((const uint8_t*)carrier+4)==24&&!niw_u32((const uint8_t*)carrier+tail+20)&&niw_u32(power)==1&&niw_u32(power+4)==1&&niw_u32(power+8)<=1){')
s=rep(s,'nss_info info={};result=nord2_wifi_scan_info(&info)?kIOReturnNotReady:kIOReturnSuccess;', '''unsigned on=niw_u32(power+8);result=nord2_wifi_power_set(on)?kIOReturnNotReady:kIOReturnSuccess;
    if(result==kIOReturnSuccess&&!on){if(pendingTicket){(void)nord2_wifi_scan_cancel(pendingTicket);pendingTicket=0;}nils_close(&cacheLife);bzero(snapshot,sizeof(*snapshot));lastScanError=kIOReturnNotReady;}''')
s=s.replace('return result; /* already physically retained ON; no event/write */','return result; /* Logical admission change; native owner drains existing firmware work. */')
s=rep(s,'uint8_t power[24]={1,0,0,0,1,0,0,0,1};memcpy','uint8_t power[24]={1,0,0,0,1,0,0,0,0};power[8]=(uint8_t)!!info.powered;memcpy');put('wifi_ios_client.cpp',s)
# Source scnFsmMsgAbort sends zeroed CMD_SCAN_CANCEL4/SET/noresponse and
# produces local cancellation; never manufacture a SCAN_DONE firmware receipt.
s=(O/'wifi_station_transport.h').read_text();s=s.replace('unsigned associated_scan_published;','unsigned associated_scan_published,associated_scan_cancel,associated_scan_canceled;');s=s.replace('int nst_associated_scan_detach(', 'int nst_associated_scan_cancel(struct nst *);\nint nst_associated_scan_detach(');put('wifi_station_transport.h',s)
s=(O/'wifi_station_transport.c').read_text();s=s.replace('case 3:return n==1188;','case 3:return n==1188;case 4:return n==4;');s=s.replace('if(cid==3&&internal!=3)return -1;','if((cid==3&&internal!=3)||(cid==4&&internal!=4))return -1;');s=s.replace('s->associated_scan_published=0;}return r;','s->associated_scan_published=s->associated_scan_cancel=s->associated_scan_canceled=0;}return r;')
s=s.replace('int nst_associated_scan_detach(','''int nst_associated_scan_cancel(struct nst*s){if(!source(s)||!s->associated_scan)return -1;s->associated_scan_cancel=1;return 0;}
int nst_associated_scan_detach(''')
s=s.replace('||!scan->done||scan->active||','||(!scan->done&&!s->associated_scan_canceled)||scan->active||');s=s.replace('s->associated_scan_published=0;return 0;','s->associated_scan_published=s->associated_scan_cancel=s->associated_scan_canceled=0;return 0;')
s=s.replace('if(s->internal_wire==1)s->peer_step++;return 0;', '''if(s->internal_wire==1)s->peer_step++;
 if(s->internal_wire==4){s->associated_scan_canceled=1;s->associated_scan->active=0;s->associated_scan->partial=1;}
 return 0;''')
s=s.replace('s->active=1;s->entered=1;s->phase=NST_IDLE;if(before(s,o,c)||coherent(s,o,c)||transmit', '''s->active=1;s->entered=1;s->phase=NST_IDLE;
 if(s->associated_scan&&s->associated_scan_cancel&&!s->associated_scan->done&&!s->associated_scan_canceled&&!busy(s)){uint8_t cancel[4]={s->associated_scan->sequence,0,0,0};if(command(s,UINT64_MAX-2,4,cancel,sizeof cancel,4))return fail(s,49);}
 if(before(s,o,c)||coherent(s,o,c)||transmit''')
s=s.replace('if(s->associated_scan&&s->associated_scan_published&&!s->associated_scan->done&&(!s->associated_scan->active||nsb_expire(s->associated_scan,s->last)))return fail(s,49);', 'if(s->associated_scan&&s->associated_scan_published&&!s->associated_scan->done&&!s->associated_scan_canceled&&(!s->associated_scan->active||nsb_expire(s->associated_scan,s->last)))s->associated_scan_cancel=1;');put('wifi_station_transport.c',s)
s=(O/'wifi_station_service_bind.inc').read_text();s=s.replace('if(!scan||!scan->done||scan->active||','if(!scan||(!scan->done&&!nst_native.state.associated_scan_canceled)||scan->active||')
s=s.replace('if((njs_native.canceled||njs_native.leave)&&!nst_native.state.associated_scan)', '''if(nst_native.state.associated_scan&&(njs_native.canceled||njs_native.leave||nst_native.state.link_lost))(void)nst_associated_scan_cancel(&nst_native.state);
 if((njs_native.canceled||njs_native.leave)&&!nst_native.state.associated_scan)''')
s=s.replace('scan_done->scan.completion.state==7&&!scan_done->scan.partial&&!scan_done->scan.dropped?NSS_COMPLETE:NSS_PARTIAL','!scan_done->scan.done?NSS_REFUSED:scan_done->scan.completion.state==7&&!scan_done->scan.partial&&!scan_done->scan.dropped?NSS_COMPLETE:NSS_PARTIAL')
s=s.replace('unsigned terminal=njs_native.driver.owner.transaction.phase==STC_DONE||njs_native.driver.owner.transaction.phase==STC_HALTED;', 'unsigned terminal=(njs_native.driver.owner.transaction.phase==STC_DONE||njs_native.driver.owner.transaction.phase==STC_HALTED)&&!nst_native.state.associated_scan;');put('wifi_station_service_bind.inc',s)
s=(O/'wifi_station_transport.c').read_text().replace('!s->associated_scan_canceled&&!busy(s)','!s->associated_scan_canceled&&!s->wire_busy&&!s->air_pending');s=s.replace('return lease_window(s,i,0)&&!s->error','return lease_window(s,i,0)&&!s->associated_scan&&!s->error');put('wifi_station_transport.c',s)
s=(O/'wifi_station_service_bind.inc').read_text();s=s.replace('int result=nwn_pump(&njs_native.driver,now_us/1000);','int result=(njs_native.canceled||njs_native.leave)&&nst_native.state.associated_scan?0:nwn_pump(&njs_native.driver,now_us/1000);');s=s.replace('if(v->data_ready)v->phase=NJS_AUTHORIZED;','if(njs_native.leave||njs_native.canceled)v->phase=NJS_LEAVING;\n else if(v->data_ready)v->phase=NJS_AUTHORIZED;');put('wifi_station_service_bind.inc',s)
s=(O/'wifi_scan_service.h').read_text().replace('unsigned scanning,powered;','unsigned scanning,powered;uint64_t power_epoch;');put('wifi_scan_service.h',s)
s=(O/'wifi_scan_service_bind.inc').read_text().replace('uint64_t serial;','uint64_t serial,power_epoch;').replace('out->powered=nss_native.powered;','out->powered=nss_native.powered;out->power_epoch=nss_native.power_epoch;').replace('nss_native.powered=1;njs_initialize_locked();','nss_native.powered=1;nss_native.power_epoch=1;njs_initialize_locked();');put('wifi_scan_service_bind.inc',s)
s=(O/'wifi_station_service_bind.inc').read_text().replace('nss_native.powered=on;', 'if(nss_native.powered!=on){if(nss_native.power_epoch==UINT64_MAX){IOLockUnlock(nss_native.lock);return 1;}nss_native.power_epoch++;}nss_native.powered=on;');put('wifi_station_service_bind.inc',s)
s=(O/'wifi_ios_client.cpp').read_text();s=s.replace('uint64_t joinTicket,joinEpoch;', 'uint64_t joinTicket,joinEpoch,powerEpoch,pendingPowerEpoch;');s=s.replace('joinTicket=joinEpoch=0;', 'joinTicket=joinEpoch=powerEpoch=pendingPowerEpoch=0;');s=s.replace(' IOReturn scanLocked(', ' void clearScanLocked();\n bool powerEventLocked();\n IOReturn scanLocked(')
s=s.replace('if(result==kIOReturnSuccess&&!on){if(pendingTicket){(void)nord2_wifi_scan_cancel(pendingTicket);pendingTicket=0;}nils_close(&cacheLife);bzero(snapshot,sizeof(*snapshot));lastScanError=kIOReturnNotReady;}', 'if(result==kIOReturnSuccess){if(!on){if(pendingTicket)(void)nord2_wifi_scan_cancel(pendingTicket);clearScanLocked();}powerEventLocked();}')
s=s.replace('IOReturn NiwlLifecycleClient::scanLocked(', '''void NiwlLifecycleClient::clearScanLocked(){nils_close(&cacheLife);if(scanGeneration!=UINT64_MAX)scanGeneration++;bzero(snapshot,sizeof(*snapshot));lastScanError=kIOReturnNotReady;}
bool NiwlLifecycleClient::powerEventLocked(){
 if(phase!=Armed||!ownsReference||!data||!state)return false;
 uint32_t*indices=(uint32_t*)state->getBytesNoCopy();uint32_t consumer=__atomic_load_n(indices,__ATOMIC_ACQUIRE);niwe_view view={consumer,producer,4096};niwe_space space={};if(niwe_plan(&view,0,0,&space)!=1)return false;
 uint8_t record[48];if(niwe_power_changed(record,sizeof record,(const uint8_t*)interfaceName))return false;
 uint8_t*ring=(uint8_t*)data->getBytesNoCopy();for(uint32_t i=0;i<space.padding;i+=4){uint32_t pad=80;memcpy(ring+producer+i,&pad,4);}for(unsigned i=0;i<48;i++)ring[(space.record_offset+i)%4096]=record[i];producer=space.next;__atomic_store_n(indices+1,producer,__ATOMIC_RELEASE);IOReturn status=sendAsyncResult64(reference,kIOReturnSuccess,nullptr,0);
 if(eventLogs++<128)printf("NORD2 WIFI IOS EVENT: event=1 published=1 notified=%u status=%#x producer=%u consumer=%u t_ms=%llu\\n",(unsigned)(status==kIOReturnSuccess),(unsigned)status,producer,consumer,niw_ms());return true;
}
IOReturn NiwlLifecycleClient::scanLocked(''')
s=s.replace('nss_info info={};nss_request request={};if(nord2_wifi_scan_info(&info))return kIOReturnNotReady;', 'nss_info info={};nss_request request={};if(nord2_wifi_scan_info(&info)||!info.powered)return kIOReturnNotReady;if(powerEpoch&&powerEpoch!=info.power_epoch)clearScanLocked();powerEpoch=info.power_epoch;')
s=s.replace('pendingTicket=ticket;pendingEpoch=generation;', 'pendingPowerEpoch=info.power_epoch;pendingTicket=ticket;pendingEpoch=generation;')
s=s.replace('IOLockLock(c->gate);nss_view view={};bool current=', 'IOLockLock(c->gate);nss_info currentPower={};bool radioCurrent=!nord2_wifi_scan_info(&currentPower)&&currentPower.powered&&currentPower.power_epoch==c->pendingPowerEpoch;nss_view view={};bool current=radioCurrent&&')
s=s.replace('if(q->request==11){if(q->payload_bytes!=1164', 'if(q->request==11){nss_info info={};if(nord2_wifi_scan_info(&info))return kIOReturnNotReady;if(!info.powered||(powerEpoch&&powerEpoch!=info.power_epoch)){clearScanLocked();powerEpoch=info.power_epoch;return (IOReturn)0xe0820446;}powerEpoch=info.power_epoch;if(q->payload_bytes!=1164')
put('wifi_ios_client.cpp',s)
s=read('wifi_ios_event.h');s=s.replace('int niwe_scan_done(', 'int niwe_power_changed(uint8_t*,size_t,const uint8_t[16]);\nint niwe_scan_done(');put('wifi_ios_event.h',s)
s=read('wifi_ios_event.c');s+='''
/* Same no-primary kind87 event shape as other source-supported notifications.
 * Event1 is POWER_CHANGED; receiver refreshes GET19, not physical power proof. */
int niwe_power_changed(uint8_t*out,size_t n,const uint8_t name[16]){
 unsigned end=0;if(!out||n<48||!name||!name[0])return -1;for(unsigned k=0;k<16;k++)if(!name[k])end=1;if(!end)return -1;
 for(unsigned k=0;k<48;k++)out[k]=0;out[0]=87;out[28]=1;for(unsigned k=0;k<16&&name[k];k++)out[32+k]=name[k];return 0;
}
''';put('wifi_ios_event.c',s)
s=(O/'wifi_station_service_bind.inc').read_text().replace('if(njs_native.leave||njs_native.canceled)v->phase=NJS_LEAVING;','if((njs_native.leave||njs_native.canceled)&&s->phase<STC_DEAUTH)v->phase=NJS_LEAVING;');put('wifi_station_service_bind.inc',s)
s=(O/'wifi_ios_client.cpp').read_text().replace('if(q->request==11){nss_info info={};','if(q->request==11){if(q->payload_bytes!=1164||a->structureOutputSize<1164)return kIOReturnBadArgument;nss_info info={};');put('wifi_ios_client.cpp',s)
s=read('wifi_live_control_bind.inc').replace('return !!(njs_native.notice_lock&&!nss_native.failed&&','return !!(nss_native.powered&&njs_native.notice_lock&&!nss_native.failed&&');put('wifi_live_control_bind.inc',s)
s=(O/'wifi_station_transport.c').read_text().replace('||!s->sta_active||!s->ptk_ready||!s->ptk.installed||!s->gtk.installed||s->channel_held','||!s->sta_active||s->channel_held');s=s.replace('int nst_associated_scan(struct nst*s', '/* Like ordinary data, source controlled-port admission is the native proof.\n * NSS additionally proves nwo_authorized, including the selected security profile. */\nint nst_associated_scan(struct nst*s');put('wifi_station_transport.c',s)
