#!/usr/bin/env python3
from pathlib import Path
import tempfile,shutil,subprocess,hashlib,json,re
R=Path(__file__).resolve().parents[5];P=Path(__file__).resolve().parent;S=P/'proposed/shim/nord2-xnu';F=P.parent/'full-tests/proposed/tools'
with tempfile.TemporaryDirectory(prefix='nord2-rxdma-realowner-') as td:
 t=Path(td);c=t/'shim/nord2-xnu';c.mkdir(parents=True)
 for f in (R/'shim/nord2-xnu').iterdir():
  if f.is_file() and f.suffix in ('.c','.h','.inc'):shutil.copyfile(f,c/f.name)
 for f in S.iterdir():
  if f.is_file():shutil.copyfile(f,c/f.name)
 shutil.copytree(F,t/'tools')
 f=c/'wifi_normal_rx.c';x=f.read_text();x=x.replace('#include "wifi_normal_rx.h"','#include "wifi_normal_rx.h"\nextern int nrd_host_probe(const struct ncd_io*,void*,struct ncr_state*,struct nrd_snapshot*,unsigned);\n#define nrd_capture nrd_host_probe');f.write_text(x)
 f=t/'tools/fixtures/nord2-wifi-normal/test-full.c';x=f.read_text().replace('static void full_test_probe(unsigned);','static void full_test_probe(unsigned);\nstatic void nrd_forbid_write(void);')
 marker='static unsigned mode,delivered,probe_memory,probe_write,delayed_trigger;'
 assert marker in x
 x=x.replace(marker,marker+'''
static unsigned nrd_in_capture,nrd_calls,nrd_restored,nrd_checked;
static void nrd_forbid_write(void){assert(!nrd_in_capture);}
int nrd_host_probe(const struct ncd_io*io,void*ctx,struct ncr_state*r,struct nrd_snapshot*out,unsigned reason){
 unsigned phase=r->phase,current=r->current,reads=mmio_reads,writes=mmio_writes,rx_index=r->init->transport.rx_index;
 struct ncr_queue queues[5];memcpy(queues,r->queue,sizeof(queues));
 struct nrd_snapshot bad={0};struct ncd_io fake=*io;
 assert(nrd_capture(&fake,ctx,r,&bad,reason)<0&&!bad.attempted);
 assert(nrd_capture(io,(char*)ctx+1,r,&bad,reason)<0&&!bad.attempted);
 assert(nrd_capture(io,ctx,r,&bad,0)<0&&!bad.attempted);
 assert(nwfn_read_allowed(0x18024680)&&nwfn_read_allowed(0x18024684)&&nwfn_read_allowed(0x18024688)&&nwfn_read_allowed(0x1802468c));
 assert(!nwfn_read_allowed(0x18024690)&&!nwfn_read_allowed(0x18025680));
 struct nwfn_native clone=nwfn_native;uint32_t value;assert(nwfn_read(&clone,0x18024680,&value)<0);
 assert(reads==mmio_reads&&writes==mmio_writes);nrd_checked++;
 nrd_in_capture=1;int result=nrd_capture(io,ctx,r,out,reason);nrd_in_capture=0;nrd_calls++;
 assert(r->phase==phase&&r->current==current&&writes==mmio_writes&&r->init->transport.rx_index==rx_index&&!memcmp(queues,r->queue,sizeof(queues)));nrd_restored++;
 if(!result){
  assert(out->attempted&&out->complete&&!out->error&&out->valid==3&&out->last>=out->started);
  assert((out->cfg[0]&5)==5&&(out->cfg[1]&5)==5);
  for(unsigned j=0;j<4;j++){
   const struct nrd_queue*q=&out->queue[j];assert(q->valid==63&&q->control[0]==q->expected_descriptors&&q->control[1]==r->queue[q->queue].ring.count&&q->ext_address==0x18024680+4*j&&q->ext==4+0x400000*j);
   assert(q->descriptor==q->expected_descriptors+16*q->next);
  }
  reads=mmio_reads;writes=mmio_writes;assert(nrd_capture(io,ctx,r,out,reason)<0&&reads==mmio_reads&&writes==mmio_writes);
 }
 return result;
}
''')
 # Source completion counters are retained raw; exercise nonzero per-channel rows.
 x=x.replace('p[41]=7;deliver(1,p,sizeof(p),1);','p[41]=7;p[42]=2;p[37]=1;p[38]=1;p[39]=6;put32(p+44,13);p[48]=1;p[52]=2;p[56]=6;p[57]=36;w16(p+120,0x1234);w16(p+122,0xabcd);p[248]=2;p[249]=3;p[312]=4;p[313]=5;put32(p+376,8);put32(p+380,5);w16(p+384,0x55aa);w16(p+386,0xa55a);deliver(1,p,sizeof(p),1);')
 x=x.replace('assert(t->pse_total==3','assert(r->scan.completion.version==2&&r->scan.completion.beacons==13&&r->scan.completion.beacons_2g==8&&r->scan.completion.beacons_5g==5&&r->scan.completion.pno==1&&r->scan.completion.channel_count==2&&r->scan.completion.channels[0]==6&&r->scan.completion.idle[1]==0xabcd&&r->scan.completion.scan_time[1]==0xa55a);\n assert(!nwfn_read_allowed(0x18024680)&&!nwfn_read_allowed(0x1802468c));\n assert(t->pse_total==3')
 # Add strong success/expiry captured-object assertions, keeping original full tests.
 x=x.replace('assert(t->pse_total==3', 'assert(r->readback[0].complete&&r->readback[0].reason==NRD_BASELINE&&r->readback[1].complete&&r->readback[1].reason==NRD_SCAN_DONE);\n assert(t->pse_total==3')
 x=x.replace('&&r->scan.count==2);','&&r->scan.count==2&&r->readback[1].complete&&r->readback[1].reason==NRD_SCAN_EXPIRED);')
 x=x.replace('puts("PASS actual22', 'assert(nrd_calls&&nrd_calls==nrd_restored&&nrd_checked==nrd_calls);\n puts("PASS readonly snapshots/phase restoration/no writes or queue mutation + actual22')
 f.write_text(x)
 # Forbidden-write check is instrumentation only, independent of authority.
 f=c/'wifi_capability_bind.inc';x=f.read_text().replace('static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{','static int ncd_native_write(void*c,uint32_t p,uint32_t v)\n{nrd_forbid_write();').replace('static int ncd_native_mwrite(void*c,uint64_t p,const void*v,uint32_t n)\n{','static int ncd_native_mwrite(void*c,uint64_t p,const void*v,uint32_t n)\n{nrd_forbid_write();');f.write_text(x)
 f=t/'tools/test-nord2-wifi-normal.py';x=f.read_text().replace("names=('wifi_normal_backend'","names=('wifi_rx_readback','wifi_normal_backend'");x=x.replace('summary=run.stdout.splitlines()[-1];',"(R/(name+'-native.log')).write_text(run.stdout);summary=run.stdout.splitlines()[-1];");f.write_text(x)
 r=subprocess.run(['python3',str(f),'--board',str(R/'research/nord2/wifi/calibration/WIFI'),'--report',str(P/'host-source-report.json')],capture_output=True,text=True)
 for name in ('full','faults'):
  f=t/(name+'-native.log')
  if f.exists():shutil.copyfile(f,P/(name+'-native.log'))
 (P/'test-output.log').write_text(r.stdout+r.stderr)
 print(r.stdout);print(r.stderr[-6000:]);r.check_returncode()
