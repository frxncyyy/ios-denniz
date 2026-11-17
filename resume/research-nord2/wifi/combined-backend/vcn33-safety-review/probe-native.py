#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile,json,hashlib
P=Path(__file__).resolve().parent;A=P.parent/'vcn33-implementation';S=A/'proposed/shim/nord2-xnu'
s=(A/'test-bind.c').read_text().split('int main(void){',1)[0]
s=s.replace('static int proof(void*c){(void)c;return 0;}','static unsigned proof_fail,waits,wait_fail;static int proof(void*c){(void)c;return proof_fail;}')
s=s.replace('(void)b;(void)o;(void)m;(void)v;return true;}','(void)b;(void)o;(void)m;(void)v;return ++waits!=wait_fail;}')
s+='''int main(int argc,char**argv){(void)argv;
 regs[0x1be8]=regs[0x1c0e]=0x8000;regs[0x1f98]=0xaf05;regs[0x1f9c]=0xbf07;
 if(argc==2){wait_fail=2;if(!setjmp(escape)){nvc_after_persistent();assert(0);}assert(ni_pwrap_owned&&writes==1&&nvc_native.state.mutated&&!nvc_native.state.complete&&nvc_native.halt&&drains==1&&panic_count==1);assert(regs[0x1f98]==0xad05&&regs[0x1f9c]==0xbf07&&regs[0x1be8]==0x8000);puts("PASS native uncertain committed write stops after firstselector, retains and drains");return 0;}
 if(argc==3){regs[0x1be6]=1;if(!setjmp(escape)){nvc_after_persistent();assert(0);}assert(!writes&&!nvc_native.state.mutated&&drains==1&&panic_count==1);puts("PASS native coldguard refusal has zeroPMICwrites and no continuation");return 0;}
 nvc_native.owner=thread;nvc_native.enabled=1;unsigned count=0;uint16_t v;
 #define GUARD(statement,restore) do{statement;unsigned old=reads;assert(nvc_read(&nvc_native,0x1be8,&v)<0&&reads==old&&!writes);restore;count++;}while(0)
 GUARD(thread=8,thread=7);GUARD(nvc_native.halt=1,nvc_native.halt=0);GUARD(npa_persistent_lifetime=false,npa_persistent_lifetime=true);GUARD(npa_native.outer_retained=false,npa_native.outer_retained=true);GUARD(npa_native.state.entered=0,npa_native.state.entered=1);GUARD(npa_native.state.fatal=1,npa_native.state.fatal=0);GUARD(npa_native.state.result.phase=9,npa_native.state.result.phase=10);GUARD(npa_native.proof=0,npa_native.proof=proof);GUARD(proof_fail=1,proof_fail=0);GUARD(nw.saved=false,nw.saved=true);GUARD(nw.domain_touched=false,nw.domain_touched=true);GUARD(ni_saved=false,ni_saved=true);GUARD(ncc_fresh_owned=false,ncc_fresh_owned=true);GUARD(ncc_latched_dirty=true,ncc_latched_dirty=false);
 assert(nvc_read(0,0x1be8,&v)<0&&!reads);nvc_native.enabled=0;assert(nvc_read(&nvc_native,0x1be8,&v)<0&&!reads);
 printf("PASS native actualbinder %u lostprerequisites +wrongcontext+disabled allrefuse beforePMIFcallback\\n",count);
}'''
with tempfile.TemporaryDirectory(prefix='vcn33-independent-') as t:
 f=Path(t)/'probe.c';f.write_text(s);b=Path(t)/'probe'
 cmd=['clang','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(S),str(f),str(S/'wifi_vcn33.c'),'-o',str(b)]
 subprocess.run(cmd,check=True)
 result=[]
 for args in ([],['commit-failure'],['cold','refusal']):
  x=subprocess.run([str(b)]+args,check=True,text=True,capture_output=True);result.append(x.stdout)
 print(json.dumps({'result':'PASS','actual_binder_sha256':hashlib.sha256((S/'wifi_vcn33_bind.inc').read_bytes()).hexdigest(),'scenarios':result,'scope':'Actual candidate native binder with synthetic PMIF callbacks; no hardware or linked-kernel claim'},indent=2))
