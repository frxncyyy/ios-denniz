/* Executes actual revision2 native binding against a strict register model. */
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include "audio_id.h"
#include "audio_i2c8.h"
#include "nord2-audio-i2c8-model.h"
typedef uintptr_t vm_offset_t;typedef size_t vm_size_t;
#define VOFF ((uintptr_t)0x700000000000ull)
static struct i2c8_model model;static uint32_t words[NAU_SNAP_COUNT];
static vm_offset_t ni_gpio=VOFF+0x10005000,ni_lt=VOFF+0x11f20000;
static unsigned opt,reads,writes,maps,logs,panic_count;static jmp_buf jump;
static void panic(const char*fmt,...){(void)fmt;panic_count++;longjmp(jump,1);}
static void PE_parse_boot_argn(const char*name,unsigned*v,size_t n){assert(!strcmp(name,"nord2audioid")&&n==4);*v=opt;}
static vm_offset_t ml_io_map(uintptr_t pa,size_t n){assert(n==0x1000);maps++;return VOFF+pa;}
static void delay(unsigned us){assert(us==10);}
static int logline(const char*fmt,...){char line[1024];va_list ap;va_start(ap,fmt);int n=vsnprintf(line,sizeof(line),fmt,ap);va_end(ap);assert(n>0&&n<255);logs++;return n;}
static uint32_t nab_mmio_read(vm_offset_t va,unsigned width){uint32_t pa=(uint32_t)(va-VOFF);reads++;
 if(width==4)for(unsigned i=0;i<NAU_SNAP_COUNT;i++)if(pa==nau_snapshot_pa[i])return words[i];
 if(width==4)for(unsigned i=0;i<NAU_AFE_COUNT;i++)if(pa==nau_afe_pa[i]){assert(!nau_afe_block(words,NAU_SNAP_COUNT));return i;}
 return m_read(&model,pa,width);
}
static void nab_mmio_write(vm_offset_t va,unsigned width,uint32_t v){writes++;m_write(&model,(uint32_t)(va-VOFF),width,v);}
#define NAB_HOST_MMIO 1
#define printf logline
#include "audio_id_bind.inc"
#undef printf
static void setup(unsigned level){m_init(&model);memset(words,0,sizeof(words));words[NAU_S_MODE_120]=model.pinmux;words[NAU_S_IFR1_CG]=model.ifr1;words[NAU_S_BM_IES122]=model.ies;words[NAU_S_DIN_96]=model.pads;opt=level;reads=writes=maps=logs=panic_count=0;memset(&nab,0,sizeof(nab));}
int main(void){
 setup(0);nab_audio_id();assert(!reads&&!writes&&!maps&&!logs&&nab_prepare_return());
 setup(3);nab_audio_id();assert(!reads&&!writes&&!maps&&!logs);
 setup(1);nab_audio_id();assert(reads&&maps==10&&!writes&&!model.starts&&!nab.probed&&nab.status==0&&nab_prepare_return());unsigned before=reads+writes;if(!setjmp(jump)){nab_audio_id();assert(0);}assert(panic_count==1&&reads+writes==before);
 setup(2);nab_audio_id();assert(nab.probed&&!nab.probe_rc&&writes&&model.ntrans==6&&nab.amp[0].rev==0xb74&&nab_prepare_return());m_check_trans(&model);memset(nab.state.snap,255,sizeof(nab.state.snap));nab_report();
 setup(2);model.behaviour=M_BUSERR;nab_audio_id();assert(nab.bus.uncertain&&nab.bus.offline&&model.starts==1);before=reads+writes;assert(!nab_prepare_return()&&!nab_prepare_return()&&before==reads+writes);
 setup(2);words[NAU_S_PWR_STATUS]=1u<<23;nab_audio_id();assert(!nab.probed&&!writes&&!model.starts);
 setup(1);words[NAU_S_IMPS_CG]=16;nab_audio_id();assert(!writes&&!nab.state.i2c8_read);
 puts("PASS revision2 actual native binding defaultoff/snapshotzero-write/PIO/once/sticky pure-return/CAM gate");return 0;
}
