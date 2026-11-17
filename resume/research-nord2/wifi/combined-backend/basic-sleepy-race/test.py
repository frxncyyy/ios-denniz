#!/usr/bin/env python3
from pathlib import Path
import tempfile,shutil,subprocess,json,hashlib
P=Path(__file__).resolve().parent;R=P.parents[4];V=P/'verification';V.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='nord2-sleepy-realowner-') as td:
 t=Path(td);shutil.copytree(R/'shim/nord2-xnu',t/'shim/nord2-xnu');shutil.copytree(R/'tools/fixtures/nord2-wifi-normal',t/'tools/fixtures/nord2-wifi-normal');shutil.copyfile(R/'tools/test-nord2-wifi-normal.py',t/'tools/test-nord2-wifi-normal.py');S=t/'shim/nord2-xnu';F=t/'tools/fixtures/nord2-wifi-normal'
 for f in (P/'proposed/shim/nord2-xnu').iterdir():shutil.copyfile(f,S/f.name)
 for f in (P/'tests').glob('*.c'):
  if f.name!='test-decoder.c':shutil.copyfile(f,F/f.name)
 f=F/'owner-fixture.inc';x=f.read_text();needle='static void simulated_completion(unsigned next){'
 helper='''static unsigned sleep_cap,sleep_pre,sleep_post,sleep_state,sleep_eid=7,sleep_bytes=40,sleep_outer=0x3800,sleep_stream,sleep_pushed,sleep_mixed,sleep_normal,sleep_delay;
static unsigned sleepy_didx(void){for(unsigned j=0;j<registers;j++)if(reg[j].address==0x1802550c)return reg[j].value;return nwfn_native.init.transport.rx_index;}
static void sleepy_at(unsigned index){struct nwt_state*t=&nwfn_native.init.transport;uint8_t*d=pool_memory+(t->rx_ring-NDP_POOL_BASE)+16*index,*p=pool_memory+(t->rx_buffers-NDP_POOL_BASE)+4096*index;
 memset(p,0,sleep_bytes);p[0]=p[1]=p[24]=p[25]=255;p[2]=(uint8_t)sleep_outer;p[3]=(uint8_t)(sleep_outer>>8);p[26]=0x34;p[27]=0x12;p[28]=(uint8_t)sleep_eid;p[29]=255;p[36]=(uint8_t)sleep_state;p[37]=p[38]=p[39]=255;if(sleep_bytes>40)memset(p+40,0xab,sleep_bytes-40);
 if(sleep_mixed&&(sleep_pushed&1)){p[28]=0x27;p[38]=3;p[44]=p[45]=0;}
 put32(d,(uint32_t)(t->rx_buffers+4096*index));put32(d+4,0xc0000000u|(sleep_bytes<<16));put32(d+8,0);put32(d+12,0);}
static void sleepy_enqueue(unsigned count){unsigned index=sleepy_didx();while(count--){sleepy_at(index);index=(index+1)%16;sleep_pushed++;}hardware_register(0x1802550c,index);}
static void sleepy_pre_basic(void){sleepy_enqueue(sleep_pre);}
'''
 assert needle in x;x=x.replace(needle,helper+needle)
 needle='  return; /* no fabricated RX or firmware ACK for CID2 */';assert needle in x;x=x.replace(needle,'  sleepy_enqueue(sleep_post);\n'+needle)
 needle=' hardware_register(0x1802541c,next);hardware_register(0x1802550c,(t->rx_index+1)%16);';assert needle in x;x=x.replace(needle,needle+'''
 if(cmd[36]==0x8a&&sleep_cap){unsigned dst=(t->rx_index+sleep_cap)%16;uint8_t*dd=pool_memory+(t->rx_ring-NDP_POOL_BASE)+16*dst,*dp=pool_memory+(t->rx_buffers-NDP_POOL_BASE)+4096*dst;memcpy(dp,event,bytes);put32(dd,(uint32_t)(t->rx_buffers+4096*dst));put32(dd+4,0xc0000000u|(bytes<<16));put32(dd+8,0);put32(dd+12,0);for(unsigned k=0;k<sleep_cap;k++)sleepy_at((t->rx_index+k)%16);hardware_register(0x1802550c,(dst+1)%16);}
''')
 needle='static void ni_wr(uintptr_t b,unsigned o,uint32_t v){uint32_t a=(uint32_t)b+o;mmio_writes++;';assert needle in x;x=x.replace(needle,needle+'if(a==0x18025508&&basic_pubs&&sleep_pushed<sleep_stream)sleepy_enqueue(1);');f.write_text(x)
 x=f.read_text();needle='static void delay(unsigned us){';assert needle in x;x=x.replace(needle,needle+'if(us==1000&&basic_pubs&&sleep_delay){sleep_delay=0;sleepy_enqueue(1);struct nwt_state*t=&nwfn_native.init.transport;uint8_t*d=pool_memory+(t->tx_ring-NDP_POOL_BASE)+16*t->tx_index;put32(d+4,get32(d+4)|0x80000000u);}')
 f.write_text(x)
 f=F/'test-full.c';x=f.read_text().replace('p[41]=7;', 'p[41]=7;')
 marker='deliver(1,p,sizeof(p),1);';assert x.count(marker)==1;x=x.replace(marker,marker+'if(sleep_normal)sleepy_enqueue(1);');f.write_text(x)
 shutil.copyfile(P/'tests/test-decoder.c',F/'test-sleepy-decoder.c')
 f=t/'tools/test-nord2-wifi-normal.py';x=f.read_text().replace("choices=('all','full','faults')","choices=('all','full','faults','sleepy','sleepy-faults','sleepy-decoder')").replace("(('full','faults') if args.only=='all'", "(('full','faults','sleepy','sleepy-faults','sleepy-decoder') if args.only=='all'")
 x=x.replace(" (d/'wifi_capability_bind.inc').write_text(adapter)"," (d/'wifi_capability_bind.inc').write_text(adapter)\n basic=(S/'wifi_basic_config_bind.inc').read_text();marker=' int result=nbc_prepare'\n if basic.count(marker)!=1:raise RuntimeError('preCID2 firmware-arrival fixture schema mismatch')\n (d/'wifi_basic_config_bind.inc').write_text(basic.replace(marker,' sleepy_pre_basic();\\n'+marker))")
 x=x.replace('list(map(str,files))',"list(map(str,[] if name=='sleepy-decoder' else files))");f.write_text(x)
 out=P/'proposed/tools'
 if out.exists():shutil.rmtree(out)
 shutil.copytree(t/'tools',out)

 flags=['clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(S)];exe=t/'decoder';subprocess.run(flags+[str(P/'tests/test-decoder.c'),'-o',str(exe)],check=True);r=subprocess.run([str(exe)],capture_output=True,text=True);(V/'decoder.log').write_text(r.stdout+r.stderr);print(r.stdout.strip());r.check_returncode()
 for name in ('sleepy','sleepy-faults','full','faults'):
  r=subprocess.run(['python3',str(f),'--board',str(R/'research/nord2/wifi/calibration/WIFI'),'--only',name,'--report',str(V/(name+'-report.json')),'--log-dir',str(V/name)],capture_output=True,text=True);(V/(name+'.log')).write_text(r.stdout+r.stderr);print(r.stdout.strip());print(r.stderr[-5000:]);r.check_returncode()
 report={'status':'PASS_SYNTHETIC_ONLY','real_native_owner':True,'canonical_mutated':False,'sources':{str(p.relative_to(P)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (P/'proposed').rglob('*') if p.is_file()}}
 (V/'author-checks.json').write_text(json.dumps(report,indent=2)+'\n')
