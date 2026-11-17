from pathlib import Path
import re,subprocess
p=Path(__file__).resolve().parent
s=(p/'sweet_touch.persistent.candidate.c').read_text()
v1=(p.parent/'sweet_touch.persistent.candidate.c').read_text()
def function(text,name):
 start=text.index(name+'('); start=text.rfind('static ',0,start); brace=text.index('{',start); depth=1;end=brace+1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
assert function(s,'nw_pmic_write')==function(v1,'nw_pmic_write')
assert 'npa_source_owner_proof, 0, 0x8fb00000, 0x8a000000, 3);' in s
real=function(s,'npa_pmic_source_enable')
prefix=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
static struct {bool saved;uint16_t pmic[6];} nw;
static uint16_t nw_addr[6]={0x1c34,0x1c3a,0x1c2e,0x1c22,0x1c28,0x1c1c};
static unsigned ni_pwrap=1;static bool ni_pwrap_owned;
static unsigned call,fail,commands;static uint16_t val,pending,trace[4];
static bool step(void){return ++call!=fail;}
static bool ni_pmic_read(uint16_t a,uint16_t *v){assert(a==nw_addr[2]||a==nw_addr[5]);if(!step())return false;*v=val;return true;}
static bool nw_wait(unsigned base,unsigned off,unsigned mask,unsigned want){assert(base==1&&off==0xca8&&mask==0x800e&&want==0x8000);return step();}
static void ni_wr(unsigned base,unsigned off,unsigned data){assert(base==1);if(!step())return;if(off==0xc84)pending=data;else{assert(off==0xc80);val=pending;trace[commands++]=val;}}
'''
suffix=r'''
static void reset(unsigned slot){nw.saved=true;nw.pmic[slot]=0x8000;val=0x8000;pending=0;call=fail=commands=0;ni_pwrap_owned=false;}
int main(void){
 for(unsigned slot=2;slot<=5;slot+=3){
 reset(slot);assert(npa_pmic_source_enable(slot));assert(commands==2&&trace[0]==0x8002&&trace[1]==0x8003&&val==0x8003&&ni_pwrap_owned);unsigned total=call;
 for(unsigned i=1;i<=total;i++){reset(slot);fail=i;assert(!npa_pmic_source_enable(slot));assert(commands<=2);}
 reset(slot);val|=1;assert(!npa_pmic_source_enable(slot)&&!commands);
 reset(slot);val^=0x100;assert(!npa_pmic_source_enable(slot)&&!commands);
 }
 reset(2);nw.saved=false;assert(!npa_pmic_source_enable(2)&&!call);
 reset(2);assert(!npa_pmic_source_enable(0)&&!call);
 puts("PASS exact extracted persistent LP-before-EN/readback order bothrails, every12callbackfault, foreignfields/initialEN/owner/slot guards; original diagnostic helper byte-identical");
}
'''
(p/'pmic-host.c').write_text(prefix+real+suffix)
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-O1','-g','-fsanitize=address,undefined',str(p/'pmic-host.c'),'-o',str(p/'pmic-host')],check=True)
subprocess.run([str(p/'pmic-host')],check=True)
