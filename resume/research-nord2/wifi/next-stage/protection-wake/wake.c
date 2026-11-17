#include "wake.h"
static const uint32_t observations[4]={0x10001220,0x10001228,0x18060184,0x180601a0};
static int snapshot(const struct npw_ops *o,void *c,uint32_t *values,unsigned *valid)
{
 *valid=0;
 for(unsigned i=0;i<4;i++){
  if(o->access(c)||o->read(c,observations[i],&values[i]))return -1;
  *valid|=1u<<i;
 }
 return 0;
}
int npw_try(const struct npw_ops *o,void *c,unsigned opt,struct npw_result *r)
{
 uint32_t v=0;int e;
 if(!r)return -1;
 *r=(struct npw_result){0};r->clean=1;
 if(opt!=1||!o||!o->access||!o->read||!o->write||!o->barrier||!o->delay_us||!o->strict_common||!o->inner){r->error=-1;return -1;}
 if(snapshot(o,c,r->before,&r->before_valid)){r->error=-2;return -1;}
 if((r->before[0]&0x6000)||(r->before[1]&0x2000)||r->before[3]){r->error=-3;return -1;}
 if(o->access(c)){r->error=-4;return -1;}
 r->touched=1;r->clean=0;
 if(o->write(c,0x180601a0,1)||o->access(c)||o->barrier(c)||o->access(c)||o->read(c,0x180601a0,&v)||v!=1){r->error=-4;goto cleanup;}
 for(unsigned i=0;i<10;i++){
  if(o->access(c)||o->read(c,0x18060184,&v)){r->error=-5;goto observe;}
  r->polls++;
  if(!(v&0x20))break;
  if(i<9&&(o->access(c)||o->delay_us(c,1000))){r->error=-5;goto observe;}
 }
 if(v&0x20){r->error=-5;goto observe;}
 for(unsigned i=0;i<10;i++){
  if(o->access(c)||o->read(c,0x18001000,&v)){r->error=-6;goto observe;}
  if(v==0x20010101)break;
 }
 if(v!=0x20010101)r->error=-6;
observe:
 if(snapshot(o,c,r->after,&r->after_valid)&&!r->error)r->error=-7;
 if(!r->error){
  if(o->strict_common(c)){r->error=-8;goto cleanup;}
  r->strict_pass=1;r->inner_called=1;r->inner_clean=0;
  e=o->inner(c,&r->inner_clean);
  if(!r->inner_clean){r->error=-9;return -1;} /*retain wake across dirtyhalt*/
  if(e)r->error=-10;
 }
cleanup:
 if(o->access(c)||o->write(c,0x180601a0,0)||o->access(c)||o->barrier(c)||o->access(c)||o->read(c,0x180601a0,&v)||v){if(!r->error)r->error=-11;return -1;}
 r->restored=1;r->clean=1;
 return r->error?-1:0;
}
