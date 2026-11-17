from pathlib import Path
import subprocess,argparse
R=Path('$NORD2_REPO');P=R/"tools";F=Path('$NORD2_REPO/research/nord2/wifi/live-control-20261023/peer-profile-candidate/owner-final-pinned/fixture');P=R/'tools'
a=argparse.ArgumentParser();a.add_argument('--board',type=Path);a.add_argument('--candidate',type=Path,default=R/'shim/nord2-xnu');a.add_argument('--log-dir',type=Path,required=True);x=a.parse_args();W=x.log_dir.resolve();W.mkdir(parents=True,exist_ok=True)
s=(P/'test-nord2-wifi-join-owner.py').read_text();start=s.index(" test+='''");end=s.index(" (d/'test.c').write_text(test)",start)
s=s[:start]+' test+='+repr((F/'owner.c').read_text())+'\n'+s[end:]
old="P=Path(__file__).resolve().parent;R=next(p for p in P.parents if (p/'AGENTS.md').exists());"
s=s.replace(old,'R=Path('+repr(str(R))+');P=Path('+repr(str(P))+');')
insert="""
 owner=(d/'owner-fixture.inc').read_text().replace('static unsigned normal_enabled,','static unsigned station_hardware;static void station_completion(unsigned);static void station_data_completion(unsigned);\\nstatic unsigned normal_enabled,').replace('if(normal_enabled&&t->completed>=5){','if(normal_enabled&&t->completed>=5){if(station_hardware){station_completion(next);return;}')
 owner=owner.replace('assert(a!=0x18024228','if(a==0x18025308&&station_hardware)station_data_completion(v);assert(a!=0x18024228')
 owner=owner.replace('static uint32_t ni_rd(uintptr_t b,unsigned o){','static unsigned peer_hw_reads,peer_selector,peer_changed,peer_selector_reads;\\nstatic uint32_t ni_rd(uintptr_t b,unsigned o){if(b==0x18434000||b==0x18438000){assert(nst_peer_model_allowed);peer_hw_reads++;if(b==0x18434000){assert(!o);return peer_selector_reads++&&peer_changed?1:peer_selector;}assert(o==0x500||o==0x508);return o==0x500?(1u<<27):(5u<<16);}')
 owner='static unsigned nst_peer_model_allowed=1;\\n'+owner
 owner=owner.replace('static void clock_interval_to_deadline(unsigned ms,unsigned scale,uint64_t*d){','static uint64_t peer_idle_deadline;static void clock_interval_to_deadline(unsigned ms,unsigned scale,uint64_t*d){if(ms==100&&scale==NSEC_PER_MSEC){*d=peer_idle_deadline=drain_now+100000000;return;}').replace('if(d==scheduler_deadline){','if(d==peer_idle_deadline)return THREAD_WAITING;if(d==scheduler_deadline){')
 (d/'owner-fixture.inc').write_text(owner)
 host=(d/'nss_host.h').read_text().replace('static void read_random(void*p,unsigned n){memset(p,0x41,n);}','static unsigned random_calls;static void read_random(void*p,unsigned n){if(n==8){memset(p,0x77,n);return;}random_calls++;memset(p,0x40+random_calls,n);}')
 (d/'nss_host.h').write_text(host)
 (d/'vectors.h').write_bytes((J/'vectors.h').read_bytes())
"""
s=s.replace(" # Existing full native fixture's probes",insert+" # Existing full native fixture's probes")
# Synthetic radio beacon becomes complete WPA2/legacy scan input, through real parser.
old='static void beacon(unsigned k,unsigned id,unsigned ddone){uint8_t p[63]={0};w16(p,63);w16(p+2,0x3801);put32(p+8,12u<<8);put32(p+12,6u<<8);p[24]=0x80;p[34]=2;p[40]=2;w16(p+56,100);p[60]=0;p[61]=1;p[62]=(uint8_t)id;deliver(k,p,sizeof(p),ddone);}'
new='static void beacon(unsigned k,unsigned id,unsigned ddone){uint8_t p[96]={0};w16(p,96);w16(p+2,0x3801);put32(p+4,4u<<11);put32(p+8,12u<<8);put32(p+12,6u<<8);p[28]=100;p[29]=255;p[32]=0x80;p[42]=2;p[48]=2;w16(p+64,100);w16(p+66,0x11);p[68]=0;p[69]=1;p[70]=(uint8_t)id;p[71]=1;p[72]=1;p[73]=0x82;const unsigned char rsnbytes[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};memcpy(p+74,rsnbytes,22);deliver(k,p,sizeof(p),ddone);}'
# Replacement occurs before generated test.c write, after base fixture loaded.
needle=" (d/'test.c').write_text(test)";s=s.replace(needle,' test=test.replace('+repr(old)+','+repr(new)+')\n'+needle)
s=s.replace("names=('wifi_wpa_diag'","names=('wifi_live_control','wifi_wpa_diag'").replace("'-DNSS_HOST_TEST'","'-DNSS_HOST_TEST','-DNWL_HOST_TEST'")
f=W/'runner.py';f.write_text(s)
subprocess.run(['python3',str(f),'--candidate',str(x.candidate.resolve()),'--log-dir',str(W)]+(['--board',str(x.board.resolve())] if x.board else []),check=True)
