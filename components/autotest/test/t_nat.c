/* Host test of bsd/dev/nord2_autotest.inc: the USB request decoder and the command thread against a made-up kernel. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
typedef void *thread_t; typedef int wait_result_t; typedef int kern_return_t; typedef uintptr_t vm_offset_t; typedef size_t vm_size_t;
#define KERN_SUCCESS 0
#define NSEC_PER_MSEC 1000000ull
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL 0
static uint64_t mach_absolute_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000ull+(uint64_t)t.tv_nsec;}
static void absolutetime_to_nanoseconds(uint64_t a,uint64_t*ns){*ns=a;}
static uint64_t g_deadline;
static void clock_interval_to_deadline(unsigned n,uint64_t scale,uint64_t*d){*d=mach_absolute_time()+n*scale;}
static int assert_wait_deadline(void*c,int i,uint64_t d){(void)c;(void)i;g_deadline=d;return 0;}
static void thread_block(int c){(void)c;uint64_t now=mach_absolute_time();if(g_deadline>now)usleep((useconds_t)((g_deadline-now)/1000));}
static void thread_deallocate(thread_t t){(void)t;}
static void *tramp(void*f){((void(*)(void*,wait_result_t))f)(NULL,0);return NULL;}
static kern_return_t kernel_thread_start(void(*f)(void*,wait_result_t),void*a,thread_t*t){(void)a;pthread_t p;*t=NULL;return pthread_create(&p,NULL,tramp,(void*)f);}
static vm_offset_t ml_io_map(vm_offset_t a,vm_size_t s){(void)a;(void)s;return 0;}
int sync_internal(void){return 0;}
static struct ev{int f,d,x,y;uint64_t ms;} evs[100000];static int nev;static int keys[64][2],nkeys;
void sweet_hid_touch(int f,int d,int x,int y){evs[nev++]=(struct ev){f,d,x,y,mach_absolute_time()/1000000};}
void sweet_hid_key(int k,int d){keys[nkeys][0]=k;keys[nkeys++][1]=d;}
static uint8_t pic[30*20*3];
static uint32_t gwant[6];
void nord2_log_stream_info(uint32_t info[2]){info[0]=0x400000;info[1]=1234;}
void *sweet_fb_snap(const uint32_t want[6],uint32_t info[8]){uint32_t scale=want[0]|want[1]<<4;memcpy(gwant,want,sizeof gwant);for(unsigned i=0;i<sizeof pic;i++)pic[i]=(uint8_t)(i*7);info[0]=30;info[1]=20;info[2]=sizeof pic;info[3]=scale;info[4]=1;info[5]=0;info[6]=1;info[7]=0;return pic;}
int nal_usb(const uint8_t s[8],uint8_t*o,size_t c,uint32_t*l){(void)o;(void)c;(void)l;return s[1]>=0x64&&s[1]<=0x66?0:-1;}
#include "../kernel/b/bsd/dev/nord2_autotest.inc"
static uint8_t out[1024];static uint32_t len;
static int rq(unsigned type,unsigned code,unsigned v,unsigned i,unsigned want){uint8_t s[8]={(uint8_t)type,(uint8_t)code,(uint8_t)v,(uint8_t)(v>>8),(uint8_t)i,(uint8_t)(i>>8),(uint8_t)want,(uint8_t)(want>>8)};return nat_usb(s,out,sizeof out,&len);}
static uint32_t w(int k){uint32_t v;memcpy(&v,out+4*k,4);return v;}
#define CHECK(c) do{if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static void run(unsigned cmd,unsigned arg,uint32_t want){CHECK(rq(0xc0,0x72,cmd,arg,8)==1&&w(0)==1);uint32_t tok=w(1);for(int i=0;i<3000;i++){CHECK(rq(0xc0,0x70,0,0,64)==1);if(w(4)==tok&&!w(2))break;usleep(1000);}CHECK(w(4)==tok&&w(5)==want);}
int main(void){
 CHECK(rq(0xc0,0x70,0,0,64)==0);                 /* not started: stall */
 CHECK(rq(0xc0,0x52,0,0,28)==-1&&rq(0xc0,0x65,0,0,8)==0);   /* others pass through / audio asked first */
 nord2_autotest_start();
 CHECK(rq(0xc0,0x70,0,0,64)==1&&len==64&&w(0)==NAT_MAGIC&&w(1)==2);CHECK(rq(0xc0,0x70,0,0,80)==1&&len==80&&(w(16)&1)&&w(17)==0);
 CHECK(rq(0x40,0x72,1,1,0)==0&&rq(0xc0,0x71,3,16,4)==0&&rq(0xc0,0x74,0,0,4)==-1&&rq(0xc0,0x73,0,0,64)==0);
 /* tap */
 unsigned t[]={100,200,100,200};for(int i=0;i<4;i++)CHECK(rq(0xc0,0x71,t[i],(unsigned)i,4)==1&&w(0)==t[i]);
 rq(0xc0,0x71,0,8,4);rq(0xc0,0x71,60,9,4);rq(0xc0,0x71,0,10,4);
 run(1,1,1);CHECK(evs[0].f==8&&evs[0].d==1&&evs[0].x==100&&evs[0].y==200&&evs[nev-1].d==0&&evs[nev-1].ms-evs[0].ms>=59&&evs[nev-1].ms-evs[0].ms<120);
 /* pinch: two fingers, monotonic, ends exactly, both lifted */
 nev=0;unsigned p[]={540,1000,540,600,540,1400,540,1800};for(int i=0;i<8;i++)rq(0xc0,0x71,p[i],(unsigned)i,4);
 rq(0xc0,0x71,300,8,4);rq(0xc0,0x71,50,9,4);rq(0xc0,0x71,100,10,4);run(1,2,1);
 int ly8=1000,ly9=1400,up=0,n8=0;for(int i=0;i<nev;i++){if(!evs[i].d){up++;continue;}if(evs[i].f==8){CHECK(evs[i].y<=ly8&&evs[i].y>=600);ly8=evs[i].y;n8++;}else{CHECK(evs[i].f==9&&evs[i].y>=ly9&&evs[i].y<=1800);ly9=evs[i].y;}}
 CHECK(up==2&&ly8==600&&ly9==1800&&n8>15&&evs[nev-1].ms-evs[0].ms>=450);
 /* busy: second command refused, abort lifts */
 nev=0;rq(0xc0,0x71,2000,9,4);CHECK(rq(0xc0,0x72,1,1,8)==1&&w(0)==1);usleep(50000);CHECK(rq(0xc0,0x72,1,1,8)==1&&w(0)==0);
 CHECK(rq(0xc0,0x72,4,0,8)==1&&w(0)==1);usleep(100000);CHECK(rq(0xc0,0x70,0,0,64)==1&&w(2)==0&&w(5)==3&&w(15)==0&&evs[nev-1].d==0);
 /* keep + continue */
 nev=0;rq(0xc0,0x71,20,9,4);rq(0xc0,0x71,0,10,4);rq(0xc0,0x71,50,8,4);run(1,1|0x100,1);CHECK(evs[nev-1].d==1);rq(0xc0,0x70,0,0,64);CHECK(w(15)==1);
 int n=nev;run(1,1|0x200,1);CHECK(evs[n].d==1&&evs[nev-1].d==0);
 /* bad coordinates, key, picture */
 rq(0xc0,0x71,1080,0,4);run(1,1,2);rq(0xc0,0x71,5,0,4);
 rq(0xc0,0x71,40,8,4);run(2,1,1);CHECK(nkeys==2&&keys[0][0]==1&&keys[0][1]==1&&keys[1][1]==0);run(2,4,2);
 rq(0xc0,0x71,10,11,4);rq(0xc0,0x71,20,12,4);rq(0xc0,0x71,300,13,4);rq(0xc0,0x71,400,14,4);run(3,2|16,1);CHECK(gwant[0]==2&&gwant[1]==1&&gwant[2]==10&&gwant[3]==20&&gwant[4]==300&&gwant[5]==400);CHECK(w(7)==1&&w(8)==30&&w(9)==20&&w(10)==sizeof pic&&(w(11)&0xff)==18&&((w(11)>>8)&0xff)==1&&w(12)==1);
 CHECK(rq(0xc0,0x73,1000,0,1024)==1&&len==800&&out[0]==(uint8_t)(1000*7));CHECK(rq(0xc0,0x73,0,0,16)==1&&len==16);CHECK(rq(0xc0,0x73,1801,0,16)==0);
 run(6,1234,6);rq(0xc0,0x70,0,0,80);CHECK(w(18)==2&&w(17)==0);   /* the host's accessor is not the known code: refused, nothing written */
 run(5,1,2);                                    /* reset without the guard word: nothing */
 printf("t_nat OK (%d touch events in the last gesture)\n",nev);return 0;}
