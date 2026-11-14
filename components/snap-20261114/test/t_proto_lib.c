/* 0640 protocol test, kernel side: the REAL bsd/dev/nord2_autotest.inc (0640 version) + the REAL compositor (sweet_fb_snapc.h) on a
 * synthetic two-layer plan with the band rotation, behind a made-up kernel.  Built as a shared library; t_proto.py drives it through
 * phone.py's own Phone methods (req() replaced by a direct call of nat_usb). */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include "sweet_fb_hw.h"
#include "sweet_fb_cutout.h"
#include "sweet_fb_snapc.h"
typedef void *thread_t; typedef int wait_result_t; typedef int kern_return_t; typedef uintptr_t vm_offset_t; typedef size_t vm_size_t;
#define KERN_SUCCESS 0
#define NSEC_PER_MSEC 1000000ull
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
static uint64_t mach_absolute_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;}
static void absolutetime_to_nanoseconds(uint64_t a,uint64_t*ns){*ns=a;}
static __thread uint64_t g_deadline;
static void clock_interval_to_deadline(unsigned n,uint64_t scale,uint64_t*d){*d=mach_absolute_time()+n*scale;}
static int assert_wait_deadline(void*c,int i,uint64_t d){(void)c;(void)i;g_deadline=d;return 0;}
static void thread_block(int c){(void)c;uint64_t now=mach_absolute_time();if(g_deadline>now)usleep((useconds_t)((g_deadline-now)/1000));}
static void thread_deallocate(thread_t t){(void)t;}
static void *tramp(void*f){((void(*)(void*,wait_result_t))f)(NULL,0);return NULL;}
static kern_return_t kernel_thread_start(void(*f)(void*,wait_result_t),void*a,thread_t*t){(void)a;pthread_t p;*t=NULL;return pthread_create(&p,NULL,tramp,(void*)f);}
static vm_offset_t ml_io_map(vm_offset_t a,vm_size_t s){(void)a;(void)s;return 0;}
int sync_internal(void){return 0;}
static uint32_t n2vc_ven_refs(void){return 0;}
static uint32_t n2j_command(uint32_t s,const uint32_t*j,const uint8_t**p,uint32_t*b){(void)s;(void)j;*p=NULL;*b=0;return 2;}
static uint32_t n2vc_at_command(uint32_t s,const uint32_t*j){(void)s;(void)j;return 2;}
/* made-up HID side: dispatch counters as SweetHID.cpp keeps them; the power key toggles the panel */
static uint32_t hc[6]; static int dark, asked_off, snap_refuse;
void sweet_hid_touch(int f,int d,int x,int y){hc[2]++;if(f>=8){hc[0]++;hc[4]=(uint32_t)x|(uint32_t)y<<16;}(void)d;}
static int key_was;
void sweet_hid_key(int k,int d){hc[1]+=1u<<16;if(k==0&&d&&!key_was){dark=!dark;asked_off=dark;}key_was=d;}
void sweet_hid_counts(uint32_t c[6]){memcpy(c,hc,sizeof hc);c[3]=1|1u<<16;}
void sweet_fb_status_words(uint32_t w[4]){w[0]=(uint32_t)asked_off|(uint32_t)dark<<1|4u|16u;w[1]=640u|700u<<16;w[2]=(uint32_t)(120*65536);w[3]=4242;}
void nord2_log_stream_info(uint32_t info[2]){info[0]=0x400000;info[1]=1234;}
int nal_usb(const uint8_t s[8],uint8_t*o,size_t c,uint32_t*l){(void)o;(void)c;(void)l;(void)s;return -1;}
/* the synthetic plan: layer 0 = full-screen gradient (pool), layer 1 = translucent box (IOVA), band rotation on */
#define FW 1080u
#define FH 2400u
static uint8_t *m0, *m1; static struct sweet_fb_hw_plan plan; static struct sweet_fb_snapc_src src[2];
static int rd(void*ctx,int s,uint64_t off,void*dst,uint32_t n){(void)ctx;uint8_t*m=s==0?m0:m1;if(off+n>src[s].len)return 1;memcpy(dst,m+off,n);return 0;}
static uint32_t line[FW]; static uint8_t tmp[SWEET_FB_SNAPC_TMP_BYTES]; static uint8_t *buf;
void t_setup(void){
	uint32_t p0=FW*4, p1=400*4;
	m0=malloc((size_t)p0*FH); m1=malloc((size_t)p1*600); buf=malloc(sweet_fb_snapc_bound(FW,FH));
	for(uint32_t y=0;y<FH;y++)for(uint32_t x=0;x<FW;x++){uint8_t*q=m0+(size_t)y*p0+4*x;q[0]=(uint8_t)(x/8);q[1]=(uint8_t)(y/10);q[2]=(uint8_t)((y/200)*20);q[3]=255;}
	for(uint32_t y=0;y<600;y++)for(uint32_t x=0;x<400;x++){uint8_t*q=m1+(size_t)y*p1+4*x;uint8_t a=(uint8_t)(((x/50)+(y/50))&1?255:96);q[0]=(uint8_t)(a*200/255);q[1]=0;q[2]=a;q[3]=a;}
	struct sweet_fb_hw_in in[2]={{0xC0100000ull,(uint64_t)p0*FH,0,p0,1,0,0,0,0,(int32_t)FW,(int32_t)FH,0},{0x90200000ull,(uint64_t)p1*600,0,p1,1,0,0,340,1000,400,600,0}};
	src[0].dev=in[0].pa;src[0].len=in[0].len;src[1].dev=in[1].pa;src[1].len=in[1].len;
	if(sweet_fb_hw_plan_makec(&plan,in,2,SWEET_FB_HW_DUAL,0,FW,FH,6)!=SWEET_FB_HW_OK){puts("plan failed");exit(1);}
	struct sweet_fb_hw_plan one; (void)sweet_fb_hw_plan_makec(&one,in,1,SWEET_FB_HW_DUAL,0,FW,FH,6);
	struct sweet_fb_cut_cfg cc=SWEET_FB_CUT_DEFAULTS; cc.on=1;
	if(sweet_fb_cut_plan(&one,in,1,&cc,0,FW,6)==SWEET_FB_CUT_OK){ /* the band pieces of a one-layer frame, added to the two-layer plan's free slots */
		for(unsigned k=0;k<2;k++)for(unsigned i=1;i<SWEET_FB_HW_NSLOT;i++)if(one.slot[k][i].en){unsigned j=plan.nslot[k]++;plan.slot[k][j]=one.slot[k][i];plan.src_con[k]|=1u<<j;}
	}
}
void t_refuse(int r){snap_refuse=r;}
int t_dark(void){return dark;}
void *sweet_fb_snap(const uint32_t want[6],uint32_t info[8]){
	if(snap_refuse){info[3]=(uint32_t)snap_refuse;info[7]=(uint32_t)asked_off|(uint32_t)dark<<1;return NULL;}
	struct sweet_fb_snapc c; (void)sweet_fb_snapc_prepare(&c,&plan,src,2,FW,FH);
	uint32_t pi[4]; sweet_fb_snapc_picture(&c,want,FW,FH,buf,line,tmp,rd,NULL,pi);
	info[0]=pi[0];info[1]=pi[1];info[2]=pi[2];info[3]=pi[3];info[4]=(uint32_t)__builtin_popcount(plan.src_con[0])+(uint32_t)__builtin_popcount(plan.src_con[1]);
	info[5]=c.flags|SWEET_FB_SNAPC_F_CUT;info[6]=0;info[7]=(uint32_t)asked_off|(uint32_t)dark<<1;return buf;}
/* the full picture at scale 1, unpacked (what every screenshot must be a sample of) */
void t_full(uint8_t *out){uint32_t w[6]={1,0,0,0,0,0},pi[4];struct sweet_fb_snapc c;(void)sweet_fb_snapc_prepare(&c,&plan,src,2,FW,FH);sweet_fb_snapc_picture(&c,w,FW,FH,out,line,tmp,rd,NULL,pi);}
#include "nord2_autotest.inc"
void t_start(void){nord2_autotest_start();}
