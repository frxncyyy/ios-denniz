from pathlib import Path
P=Path(__file__).resolve().parent;R=next(p for p in P.parents if(p/'AGENTS.md').exists());F=P/'tests/fixtures/nord2-wifi-rejoin';F.mkdir(parents=True,exist_ok=True)
s=(R/'tools/fixtures/nord2-wifi-rejoin/owner.c').read_text();s=s[:s.index(' /* Third real Join')]
helper='''static unsigned associated_callbacks,associated_canceled,scan_hold,held_sequence,cancel_commands,hold_scan_dma,held_scan_next;
static void canceled_completed(void*c,uint64_t g,uint64_t ticket,enum nss_status status){assert(c==&associated_canceled&&g==89&&status==NSS_CANCELED);associated_canceled++;assert(!nord2_wifi_scan_release(ticket));}
static void associated_completed(void*c,uint64_t g,uint64_t ticket,enum nss_status status){
 assert(c==&associated_callbacks&&g==88);assert(status==NSS_COMPLETE);struct nss_view v={0};assert(!nord2_wifi_scan_view(ticket,&v)&&v.scan->done&&v.scan->count==1&&v.scan->completion.state==7);assert(njs_native.linked&&nst_native.state.controlled_port&&nst_native.state.ptk.installed&&nst_native.state.gtk.installed);associated_callbacks++;assert(!nord2_wifi_scan_release(ticket));}
static void associated_frames(unsigned seq){
 uint8_t*p=station_packet(2,7,96);w16(p+2,0x3801);put32(p+4,4u<<11);put32(p+8,12u<<8);put32(p+12,6u<<8);p[28]=100;p[29]=255;p[32]=0x80;p[42]=2;p[48]=2;w16(p+64,100);w16(p+66,0x11);p[68]=0;p[69]=1;p[70]='X';p[71]=1;p[72]=1;p[73]=0x82;memcpy(p+74,rsn,22);
 p=station_packet(4,7,512);p[28]=13;p[36]=(uint8_t)seq;p[40]=1;p[41]=7;p[42]=2;p[52]=1;p[56]=6;
}
'''
s='static unsigned lifecycle_rx;\n'+s
s=s.replace('return 0;}\nstatic void drain', 'lifecycle_rx++;return 0;}\nstatic void drain')
vec=(R/'tools/fixtures/nord2-wifi-gtk-rekey/gtk-vectors.h').read_text();vec=vec[vec.index('static const unsigned char g1_0[]'):vec.index('static const unsigned char g1_1[]')]
helper=helper.replace('static void associated_frames',vec+'\nstatic void make_group(uint8_t*out){uint8_t plain[64],mic[20];size_t used=0;struct nwp*w=&njs_native.driver.owner.transaction.wpa;assert(!nwpc_unwrap(ptk+16,g1_0+99,sizeof g1_0-99,plain,sizeof plain,&used));memcpy(out,g1_0,sizeof g1_0);EVP_CIPHER_CTX*c=EVP_CIPHER_CTX_new();assert(c);EVP_CIPHER_CTX_set_flags(c,EVP_CIPHER_CTX_FLAG_WRAP_ALLOW);int n=0,end=0;assert(EVP_EncryptInit_ex(c,EVP_aes_128_wrap(),0,w->kek,0)&&EVP_EncryptUpdate(c,out+99,&n,plain,(int)used)&&EVP_EncryptFinal_ex(c,out+99+n,&end)&&n+end==(int)sizeof g1_0-99);EVP_CIPHER_CTX_free(c);memset(out+81,0,16);const uint8_t*parts[]={out};size_t sizes[]={sizeof g1_0};assert(!nwpc_hmac(w->kck,16,parts,sizes,1,mic));memcpy(out+81,mic,16);niwj_clear(plain,sizeof plain);niwj_clear(mic,sizeof mic); }\nstatic void associated_frames')
s=s.replace('static void station_completion(unsigned next){',helper+'\nstatic void station_completion(unsigned next){')
s=s.replace('if(p[36]==0x1c&&!p[66])','if(p[36]==3){assert(njs_native.linked&&nst_native.state.controlled_port);if(scan_hold)held_sequence=p[64];else associated_frames(p[64]);if(hold_scan_dma){held_scan_next=next;return;}}\n if(p[36]==4){assert(p[64]==held_sequence&&!p[65]&&!p[66]&&!p[67]);cancel_commands++;}\n if(p[36]==0x1c&&!p[66])')
needle='  /* Pending DMA/keys/port/cookie cannot become a reusable session. */'
s=s.replace(needle,'''  struct nss_request aq={0};aq.count=1;aq.channels[0]=nss_native.info.channels[0];uint64_t scan_ticket=0;unsigned commands_before=station_commands;
  scan_hold=hold_scan_dma=1;held_scan_next=held_sequence=0;assert(!nord2_wifi_scan_enqueue(&aq,&associated_callbacks,88,associated_completed,&scan_ticket));
  for(unsigned k=0;k<100&&!held_sequence;k++)assert(njs_owner_work()==1);
  assert(held_scan_next&&held_sequence&&nst_native.state.associated_scan&&nst_native.state.associated_scan->active&&njs_native.linked&&nst_native.state.controlled_port);
  {unsigned group_before=nst_native.state.gtk_submission_serial;uint8_t group[sizeof g1_0];make_group(group);station_eapol(group,sizeof group);assert(njs_owner_work()==1&&njs_native.driver.owner.transaction.wpa.group_phase==1&&nst_native.state.gtk_submission_serial==group_before);struct nst*ns=&nst_native.state;uint8_t*d=pool_memory+(ns->init->transport.tx_ring-NDP_POOL_BASE)+16*ns->tx_next;put32(d+4,0xc0000000u);hardware_register(0x1802541c,held_scan_next);hold_scan_dma=0;for(unsigned k=0;k<100&&(njs_native.driver.owner.transaction.wpa.group_phase||nst_native.state.wire_busy);k++)assert(njs_owner_work()==1);assert(nst_native.state.gtk_submission_serial==group_before+1&&njs_native.linked&&nst_native.state.controlled_port);}
  assert(!nord2_wifi_link_send(data,sizeof data,ticket));for(unsigned k=0;k<100&&(njs_native.tx_count||nst_native.state.wire_busy);k++)assert(njs_owner_work()==1);assert(nst_native.state.associated_scan->active);
  {struct nst*ns=&nst_native.state;uint8_t*r=station_packet(0,2,116);put32(r+4,(9u<<11)|(4u<<16)|ns->peer.wlan);put32(r+8,(1u<<13)|(7u<<8));w16(r+24,0x4208);memcpy(r+26,ns->peer.ap,6);r[40]=1;memcpy(r+56,ns->peer.self,6);memcpy(r+62,ns->peer.ap,6);r[68]=8;r[69]=6;unsigned before_rx=lifecycle_rx;assert(njs_owner_work()==1&&lifecycle_rx==before_rx+1);}
  associated_frames(held_sequence);scan_hold=0;
  for(unsigned k=0;k<100&&!associated_callbacks;k++)assert(njs_owner_work()==1);
  assert(associated_callbacks==cycle+1&&station_commands==commands_before+2&&njs_native.linked&&nst_native.state.sta_active&&!nst_native.state.associated_scan);
'''+needle)
s=s.replace('for(unsigned k=0;k<100&&!associated_callbacks;k++)','for(unsigned k=0;k<100&&associated_callbacks<cycle+1;k++)')
s=s.replace('assert(!njs_rejoin_ready_locked());assert(!nord2_wifi_join_disconnect());','''assert(!njs_rejoin_ready_locked());
  scan_hold=1;held_sequence=0;uint64_t canceled_ticket=0;assert(!nord2_wifi_scan_enqueue(&aq,&associated_canceled,89,canceled_completed,&canceled_ticket));for(unsigned k=0;k<100&&!held_sequence;k++)assert(njs_owner_work()==1);assert(nst_native.state.associated_scan->active);unsigned before_delete=key_deletes;
  assert(!nord2_wifi_power_set(0));assert(!nst_native.state.controlled_port&&!njs_native.linked);for(unsigned k=0;k<100&&associated_canceled<cycle+1;k++)assert(njs_owner_work()==1);assert(cancel_commands==cycle+1&&associated_canceled==cycle+1&&key_deletes==before_delete&&!nst_native.state.associated_scan);scan_hold=0;struct nss_info offinfo;assert(!nord2_wifi_scan_info(&offinfo)&&!offinfo.powered);assert(nord2_wifi_scan_enqueue(&aq,&associated_callbacks,88,associated_completed,&scan_ticket)==2);assert(nord2_wifi_link_send(data,sizeof data,ticket)==NWIL_SEND_NOT_READY);uint64_t refused=999;assert(nord2_wifi_join_enqueue(&q,notices,7,join_completed,&refused)==-1||refused==999);
  assert(!nord2_wifi_power_set(1));assert(!nord2_wifi_scan_info(&offinfo)&&offinfo.powered);''')
s+=''' assert(associated_callbacks==2&&associated_canceled==2&&lifecycle_rx==2&&station_pubs>station_frames&&link_up_calls==2&&link_down_calls>=2&&random_calls==2&&delivered&&nrd_calls==nrd_restored&&nrd_checked==nrd_calls);assert(!clock_expire);
 IOLock*notice=njs_native.notice_lock;assert(!pthread_mutex_destroy(&notice->m));free(notice);assert(!pthread_mutex_destroy(&nss_native.lock->m));free(nss_native.lock);
 puts("PASS actual two-WPA2-epoch lifecycle: associated CID3 scan and real RX completion/retained keys/port, logical OFF admission+native teardown, ON fresh scan/rejoin; synthetic AP only");return 0;}\n'''
(F/'owner.c').write_text(s)
runner=(R/'tools/test-nord2-wifi-rejoin.py').read_text();(P/'tests/run.py').write_text(runner)
# Final generated-fixture adjustments are kept explicit and reusable.
f=F/'owner.c';s=f.read_text();s=s.replace('key_deletes==(cycle+1)*2','key_deletes==(cycle+1)*3')
s=s.replace('int main(int argc,char**argv){assert(argc==2);','int main(int argc,char**argv){setvbuf(stdout,0,_IONBF,0);assert(argc==2);')
s=s.replace('static uint8_t*station_packet', 'static int checked_owner(void){int r=njs_owner_work();if(r!=1)fprintf(stderr,"OWNER r=%d native_error=%u halt=%u stc=%u ownerhalt=%u ledgerbad=%u scan=%u\\n",r,nst_native.state.error,nst_native.state.halt,njs_native.driver.owner.transaction.phase,njs_native.driver.owner.halted,nsr_native.bad,!!nst_native.state.associated_scan);return r;}\nstatic uint8_t*station_packet')
s=s.replace('assert(njs_owner_work()==1','assert(checked_owner()==1');f.write_text(s)
f=F/'owner.c';s=f.read_text();s=s.replace('static unsigned lifecycle_rx;','#include <unistd.h>\n#include <sys/wait.h>\nstatic unsigned lifecycle_rx,timeout_callbacks;')
s=s.replace('static void associated_completed(', 'static void timeout_completed(void*c,uint64_t g,uint64_t t,enum nss_status status){assert(c==&timeout_callbacks&&g==90&&status==NSS_REFUSED);struct nss_view v;assert(nord2_wifi_scan_view(t,&v)==1);timeout_callbacks++;assert(!nord2_wifi_scan_release(t));}\nstatic void associated_completed(')
s=s.replace('  associated_frames(held_sequence);scan_hold=0;', '''  associated_frames(held_sequence-1);assert(checked_owner()==1&&associated_callbacks==cycle&&nst_native.state.associated_scan->active); /* actual stale SCAN_DONE does not complete */
  associated_frames(held_sequence);scan_hold=0;''')
s=s.replace('  /* Pending DMA/keys/port/cookie cannot become a reusable session. */', '''  /* Missing SCAN_DONE takes actual source CID04 path, no fabricated DONE. */
  scan_hold=1;held_sequence=0;uint64_t timeout_ticket=0;assert(!nord2_wifi_scan_enqueue(&aq,&timeout_callbacks,90,timeout_completed,&timeout_ticket));for(unsigned k=0;k<100&&!held_sequence;k++)assert(checked_owner()==1);drain_now+=UINT64_C(14001000000);for(unsigned k=0;k<100&&timeout_callbacks<cycle+1;k++)assert(checked_owner()==1);assert(timeout_callbacks==cycle+1&&njs_native.linked&&nst_native.state.controlled_port&&!nst_native.state.associated_scan&&!nst_native.state.halt);scan_hold=0;
  /* Pending DMA/keys/port/cookie cannot become a reusable session. */''')
s=s.replace('cancel_commands==cycle+1','cancel_commands==(cycle+1)*2')
s=s.replace('associated_callbacks==2&&associated_canceled==2', 'associated_callbacks==2&&associated_canceled==2&&timeout_callbacks==2')
# An isolated fork begins from the genuine initialized retained owner. No new
# native prepare/DMA is permitted for a queued-but-never-started join canceled OFF.
s=s.replace(' /* Actual received synthetic beacon exposes real WPA2 selection inputs. */', ''' {pid_t child=fork();assert(child>=0);if(!child){struct niwj_request queued=request();uint64_t jt=0;unsigned pubs=station_pubs;assert(!nord2_wifi_join_enqueue(&queued,notices,7,join_completed,&jt));niwj_clear(&queued,sizeof queued);assert(!nord2_wifi_power_set(0));assert(checked_owner()==1&&!njs_native.queued&&!njs_native.active&&!nst_native.attempted&&station_pubs==pubs&&notices[NJS_JOIN_FAILED]==1&&notices[NJS_RETIRED]==1);assert(!nord2_wifi_power_set(1));_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));}
 /* Actual received synthetic beacon exposes real WPA2 selection inputs. */''')
f.write_text(s)
f=F/'owner.c';s=f.read_text();s=s.replace('static void associated_completed(', 'static unsigned off_nct_callbacks;static void off_nct_completed(void*c,uint64_t g,uint64_t t,enum nss_status status){assert(c==&off_nct_callbacks&&g==87&&status==NSS_CANCELED);off_nct_callbacks++;assert(!nord2_wifi_scan_release(t));}\nstatic void associated_completed(')
s=s.replace(' /* Actual received synthetic beacon exposes real WPA2 selection inputs. */', ''' {pid_t child=fork();assert(child>=0);if(!child){struct nss_request rq={0};uint64_t t=0;assert(!nord2_wifi_scan_enqueue(&rq,&off_nct_callbacks,87,off_nct_completed,&t));lifecycle_off_nct=1;drain();struct nss_info info;assert(off_nct_callbacks==1&&!lifecycle_off_nct&&!nord2_wifi_scan_info(&info)&&!info.powered&&nct_done_receipt(&nct_native.session)&&!nst_native.attempted&&!nss_native.failed);assert(!nord2_wifi_power_set(1));_exit(0);}int status;assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));}
 /* Actual received synthetic beacon exposes real WPA2 selection inputs. */''');f.write_text(s)
f=P/'tests/run.py';s=f.read_text();needle=" (d/'owner-fixture.inc').write_text(owner)";s=s.replace(needle," owner='static unsigned lifecycle_off_nct;extern int nord2_wifi_power_set(unsigned);'+owner\n owner=owner.replace('if(normal_enabled&&t->completed>=5){','if(normal_enabled&&t->completed>=5){if(lifecycle_off_nct){lifecycle_off_nct=0;assert(!nord2_wifi_power_set(0));}')\n"+needle);f.write_text(s)
