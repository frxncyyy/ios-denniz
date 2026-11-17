/* Executes actual revision3 native binding against a strict register model. */
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
static void setup(unsigned level){
    m_init(&model);memset(words,0,sizeof(words));
    words[NAU_S_MODE_120]=model.pinmux;words[NAU_S_IFR1_CG]=model.ifr1;words[NAU_S_IFR2_CG]=model.ifr2;
    words[NAU_S_BM_IES122]=model.ies;words[NAU_S_DIN_96]=model.pads;
    opt=level;reads=writes=maps=logs=panic_count=0;memset(&nab,0,sizeof(nab));
}
int main(void){
    unsigned before;
    setup(0);nab_audio_id();assert(!reads&&!writes&&!maps&&!logs&&nab_prepare_return());
    setup(3);nab_audio_id();assert(!reads&&!writes&&!maps&&!logs);
    setup(1);nab_audio_id();assert(reads&&maps==10&&!writes&&!model.starts&&!nab.probed&&nab.status==0&&nab_prepare_return());
    before=reads+writes;if(!setjmp(jump)){nab_audio_id();assert(0);}assert(panic_count==1&&reads+writes==before);
    setup(2);nab_audio_id();assert(nab.probed&&!nab.probe_rc&&writes&&model.ntrans==6&&nab.amp[0].rev==0xb74&&nab_prepare_return());m_check_trans(&model);m_check_restored(&model);memset(nab.state.snap,255,sizeof(nab.state.snap));nab_report();
    /* Clean NACK on amp0: amp1 still identified, Linux return allowed. */
    setup(2);model.slave[0].nack=1;nab_audio_id();
    assert(nab.probed&&!nab.probe_rc&&nab.bus.nacks==1&&!nab.bus.uncertain&&model.ntrans==4);
    assert(nab.amp[0].cls==NA8_NACK&&nab.amp[1].cls==NA8_SUPPORTED&&nab_prepare_return());m_check_trans(&model);m_check_restored(&model);
    /* Hard completion error, then a healthy model: pure refusal, no retry. */
    setup(2);model.behaviour=M_BUSERR;nab_audio_id();assert(nab.bus.uncertain&&nab.bus.offline&&model.starts==1);
    before=reads+writes;model.behaviour=M_NORMAL;
    assert(!nab_prepare_return()&&!nab_prepare_return()&&before==reads+writes);
    /* Timeout is also uncertain; flipping the model changes nothing. */
    setup(2);model.behaviour=M_HANG;nab_audio_id();assert(nab.bus.uncertain&&!nab_prepare_return());
    before=reads+writes;model.behaviour=M_NORMAL;
    assert(!nab_prepare_return()&&!nab_prepare_return()&&before==reads+writes);
    /* I3C_EN lives in AP DMA_FSM_DEBUG 0xec, not DEBUGSTAT 0xe4. */
    setup(2);model.ch[1][M_V2_DMA_FSM_DEBUG/2]=0x8000;nab_audio_id();
    assert(!writes&&!model.starts&&nab.bus.failure==21&&nab.bus.offline&&!nab.bus.uncertain&&nab_prepare_return());
    setup(2);model.ch[1][M_V2_DEBUGSTAT/2]=0x8000;nab_audio_id();
    assert(nab.probed&&!nab.probe_rc&&model.starts&&nab.bus.i3c==0&&nab_prepare_return());
    /* CAM power blocks the probe. */
    setup(2);words[NAU_S_PWR_STATUS]=1u<<23;nab_audio_id();assert(!nab.probed&&!writes&&!model.starts);
    /* Gated I2C8 leaf: snapshot only. */
    setup(1);words[NAU_S_IMPS_CG]=16;nab_audio_id();assert(!writes&&!nab.state.i2c8_read);
    /* Binding setup allowlist: fixed slave/control values, restore any saved word. */
    setup(1);nab_audio_id();memset(&nab.bus,0,sizeof(nab.bus));
    assert(nab_bus_write(0,0x11d04104,2,0x34u<<1)==0);
    assert(nab_bus_write(0,0x11d04104,2,0x51u<<1)==-1);
    assert(nab_bus_write(0,0x11d04108,2,1)==-1);
    assert(nab_bus_write(0,0x11d04110,2,0x3e)==-1);
    assert(nab_bus_write(0,0x11d0414c,2,0x0182)==0);
    assert(nab_bus_write(0,0x11d04100,1,0x51)==-1);
    nab.bus.restoring=1;
    assert(nab_bus_write(0,0x11d04104,2,0x51u<<1)==0);
    assert(nab_bus_write(0,0x11d04108,2,0x129)==0);
    puts("PASS revision3 actual native binding defaultoff/snapshotzero-write/PIO/once/"
        "sticky pure-return/NACK/I3C/setup-allowlist/CAM gate");return 0;
}
