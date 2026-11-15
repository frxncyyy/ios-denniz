/* Host test of AppleCLCD::snap (0296) cut out of SweetFramebuffer.cpp: plain and packed pictures, parts, scales. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
typedef size_t vm_size_t;
static void *IOMallocData(vm_size_t n){return malloc(n);}
struct AppleCLCD{uint32_t fWidth,fHeight,fRowBytes,fPresents;uint8_t fFront;bool fDbl,fPowerOff;uintptr_t fPixels,fPixelsB;uint8_t *snap(const uint32_t want[6],uint32_t info[8]);};
#include "snap_body.inc"
#define CHECK(c) do{if(!(c)){printf("FAIL line %d: %s\n",__LINE__,#c);exit(1);}}while(0)
int main(){
 const uint32_t W=1080,H=2400;std::vector<uint32_t> fb((W+8)*H),fbB((W+8)*H);
 uint32_t seed=1;auto rnd=[&]{seed=seed*1664525u+1013904223u;return seed>>8;};
 for(uint32_t y=0;y<H;y++)for(uint32_t x=0;x<W+8;x++){uint32_t v=y<800?0xff000000u:(y<1600?(0xff000000u|((x/37)*0x010203u&0xffffff)):(0xff000000u|(rnd()&0xffffff)));fb[y*(W+8)+x]=v;fbB[y*(W+8)+x]=v^0x00ffffffu;}
 AppleCLCD c{W,H,(W+8)*4,7,0,true,false,(uintptr_t)fb.data(),(uintptr_t)fbB.data()};
 struct{uint32_t w[6];}cases[]={{{1,0,0,0,0,0}},{{1,1,0,0,0,0}},{{2,1,0,0,0,0}},{{4,0,0,0,0,0}},{{8,1,0,0,0,0}},{{1,1,100,700,333,1000}},{{3,1,5,1590,1075,810}},{{2,1,1079,2399,50,50}},{{1,1,0,1600,1080,800}},{{9,1,2000,0,10,10}}};
 for(auto&k:cases)for(int front=0;front<2;front++){
  c.fFront=(uint8_t)front;uint32_t info[8];uint8_t*p=c.snap(k.w,info);CHECK(p);
  uint32_t scale=k.w[0]>=1&&k.w[0]<=8?k.w[0]:2,x0=k.w[2],y0=k.w[3],pw=k.w[4],ph=k.w[5];
  if(!pw||!ph||x0>=W||y0>=H){x0=y0=0;pw=W;ph=H;} if(pw>W-x0)pw=W-x0; if(ph>H-y0)ph=H-y0;
  uint32_t w=pw/scale,h=ph/scale;CHECK(info[0]==w&&info[1]==h&&info[3]==(scale|k.w[1]<<4)&&info[4]==(uint32_t)front);
  std::vector<uint8_t> rgb;
  if(k.w[1]){uint32_t i=0;while(i<info[2]){uint8_t ct=p[i++];if(ct<128){rgb.insert(rgb.end(),p+i,p+i+3*(ct+1));i+=3*(ct+1);}else{for(int r=0;r<ct-126;r++)rgb.insert(rgb.end(),p+i,p+i+3);i+=3;}}CHECK(i==info[2]);CHECK(info[2]<=(uint64_t)w*h*3+(uint64_t)w*h/64+64);}
  else rgb.assign(p,p+info[2]);
  CHECK(rgb.size()==(size_t)w*h*3);
  const std::vector<uint32_t>&s=front?fbB:fb;
  for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){uint32_t v=s[(y0+y*scale)*(W+8)+x0+x*scale];const uint8_t*q=&rgb[(y*w+x)*3];CHECK(q[0]==((v>>16)&255)&&q[1]==((v>>8)&255)&&q[2]==(v&255));}
  if(front==0)printf("scale %u packed %u part %u,%u %ux%u -> %ux%u, %u bytes (%.0f%% of plain)\n",scale,k.w[1],x0,y0,pw,ph,w,h,info[2],(w&&h)?100.0*info[2]/((double)w*h*3):0.0);
 }
 printf("t_snap OK\n");return 0;}
