#include "wake.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct fake {unsigned calls,fail,persistent,writes,reads,polls,delays;uint32_t request,ack,wake;int clears_ack,hostbusy,idbad,innerdirty,innererror,innercalled;};
static int fault(struct fake*f){return ++f->calls==f->fail||(f->persistent&&f->fail&&f->calls>f->fail);}
static int access_(void*c){return fault(c);}
static int read_(void*c,uint32_t a,uint32_t*v){struct fake*f=c;f->reads++;if(fault(f))return -1;switch(a){case 0x10001220:*v=f->request;return 0;case 0x10001228:*v=f->ack;return 0;case 0x18060184:*v=f->hostbusy?0x20:0;return 0;case 0x180601a0:*v=f->wake;return 0;case 0x18001000:*v=f->idbad?0:0x20010101;return 0;default:assert(0);return -1;}}
static int write_(void*c,uint32_t a,uint32_t v){struct fake*f=c;assert(a==0x180601a0&&v<=1);f->writes++;f->wake=v;if(v&&f->clears_ack)f->ack&=~0x4000;return fault(f)?-1:0;}
static int barrier(void*c){return fault(c);}
static int delay_(void*c,unsigned us){struct fake*f=c;assert(us==1000);f->delays++;return fault(f);}
static int strict(void*c){struct fake*f=c;if(fault(f))return -1;return f->ack&0x6000?-1:0;}
static int inner(void*c,int*clean){struct fake*f=c;assert(f->wake==1&&!(f->ack&0x6000));f->innercalled++;*clean=!f->innerdirty;return fault(f)?-1:f->innererror;}
static const struct npw_ops ops={access_,read_,write_,barrier,delay_,strict,inner};
static void init(struct fake*f){memset(f,0,sizeof(*f));f->ack=0xe70c500e;f->clears_ack=1;}
int main(void){struct fake f;struct npw_result r;unsigned total;
 init(&f);assert(!npw_try(&ops,&f,1,&r)&&r.clean&&r.restored&&r.inner_called&&f.writes==2&&!f.wake&&r.before_valid==15&&r.after_valid==15);total=f.calls;
 init(&f);f.clears_ack=0;assert(npw_try(&ops,&f,1,&r)==-1&&r.clean&&!r.inner_called&&r.error==-8&&!f.wake); /*noassumedfix*/
 init(&f);f.request=0x4000;assert(npw_try(&ops,&f,1,&r)==-1&&r.clean&&!f.writes);
 init(&f);f.wake=1;assert(npw_try(&ops,&f,1,&r)==-1&&r.clean&&!f.writes);
 init(&f);f.hostbusy=1;assert(npw_try(&ops,&f,1,&r)==-1&&r.clean&&r.polls==10&&f.delays==9&&!r.inner_called);
 init(&f);f.idbad=1;assert(npw_try(&ops,&f,1,&r)==-1&&r.clean&&!r.inner_called);
 init(&f);f.innerdirty=1;assert(npw_try(&ops,&f,1,&r)==-1&&!r.clean&&f.wake==1&&f.writes==1&&!r.restored);
 for(unsigned i=1;i<=total;i++)for(unsigned permanent=0;permanent<2;permanent++){
  init(&f);f.fail=i;f.persistent=permanent;assert(npw_try(&ops,&f,1,&r)==-1);if(r.clean)assert(!f.wake);if(!r.clean)assert(r.touched&&!r.restored);
 }
 for(unsigned i=0;i<4;i++)if(i!=1){init(&f);assert(npw_try(&ops,&f,i,&r)==-1&&!f.calls&&!f.writes);}
 printf("PASS experimentalhold/nonclearingACK;all%u transient/permanent callbackfaults;exactallowlist;snapshotbeforewrite;strictguardrequired;dirtyretain;boundedpoll;optout\n",total);return 0;}
