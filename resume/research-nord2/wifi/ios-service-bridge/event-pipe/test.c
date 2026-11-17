#include "pipe.h"
#include <assert.h>
#include <string.h>
static void p32(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
int main(void){uint8_t p[256]={0},out[16]={0};struct niwe_record r,old;struct niwe_space q;struct niwe_view v={0,0,256};
 memset(&r,0x55,sizeof r);old=r;assert(niwe_peek(p,256,&v,&r)==0&&!memcmp(&r,&old,sizeof r));
 p32(p,87);p32(p+24,4);p32(p+28,10);p32(p+48,0x12345678);v.producer=52;
 assert(niwe_peek(p,256,&v,&r)==2&&r.next==52&&r.first_bytes==4&&!r.second_bytes);
 assert(!niwe_copy_primary(p,256,&v,&r,out,4)&&out[0]==0x78);
 out[0]=99;assert(niwe_copy_primary(p,256,&v,&r,out,3)==-1&&out[0]==99);
 r.next++;assert(niwe_copy_primary(p,256,&v,&r,out,16)==-1);r.next--;
 v.producer=48;assert(niwe_peek(p,256,&v,&r)==-1);v.producer=52;
 p32(p+24,5);assert(niwe_peek(p,256,&v,&r)==-1);p32(p+24,4);
 for(unsigned c=208;c<256;c+=4){memset(p,0,sizeof p);v.consumer=c;v.producer=4;p32(p+c,80);assert(niwe_peek(p,256,&v,&r)==1&&r.next==(c+4)%256);}
 memset(p,0,sizeof p);v.consumer=204;v.producer=12;p32(p+204,87);p32(p+228,16);p32(p+232,10);
 for(unsigned i=0;i<16;i++)p[(252+i)%256]=(uint8_t)(i+1);
 assert(niwe_peek(p,256,&v,&r)==2&&r.first_bytes==4&&r.second_bytes==12&&r.next==12);
 assert(!niwe_copy_primary(p,256,&v,&r,out,16));for(unsigned i=0;i<16;i++)assert(out[i]==i+1);
 for(unsigned c=0;c<256;c+=4)for(unsigned prod=0;prod<256;prod+=4){v.consumer=c;v.producer=prod;int z=niwe_plan(&v,4,0,&q);assert(z>=0);if(z==1){assert(q.next!=c&&q.total+q.padding<=q.free_bytes&&!(q.next&3));}}
 v.consumer=0;v.producer=252;memset(&q,0x55,sizeof q);struct niwe_space before=q;assert(niwe_plan(&v,4,0,&q)==0&&!memcmp(&q,&before,sizeof q));
 v.consumer=3;assert(niwe_plan(&v,4,0,&q)==-1);v.consumer=0;v.capacity=0;assert(niwe_peek(p,256,&v,&r)==-1);uint8_t name[16]={1};assert(!niwe_scan_done(p,52,name,7));v=(struct niwe_view){0,52,256};assert(niwe_peek(p,256,&v,&r)==2&&r.event==10&&r.primary==4&&r.interface_name[0]==1);assert(!niwe_copy_primary(p,256,&v,&r,out,16)&&out[0]==7);return 0;}
