#ifndef TEGU_FIXED_UI_CONSTANT_TRACE_H
#define TEGU_FIXED_UI_CONSTANT_TRACE_H
#include <stddef.h>
#include <stdint.h>
extern int snprintf(char *,size_t,const char *,...);

/* One bounded, parseable record for a function-constant snapshot. The caller
 * supplies the specialization serial so records from concurrent callers can be
 * grouped without retaining any caller-owned strings or bytes. */
static size_t tegu_fixed_ui_constant_trace_format(
 char *line,size_t capacity,const char *entry,unsigned serial,size_t ordinal,
 uint32_t kind,uint32_t index,const uint8_t *name,size_t name_len,
 uint32_t type,const uint8_t *data,size_t size)
{
 if(!line||!capacity||!entry||!data||!size||size>64||kind>1||
    (kind==1&&(!name||!name_len)))return 0;
 int n=snprintf(line,capacity,"TEGU_FIXED_UI_FC entry=%s serial=%u ordinal=%lu selector=",
  entry,serial,(unsigned long)ordinal);
 if(n<0||(size_t)n>=capacity)return 0;
 size_t used=(size_t)n;
 if(kind==0)n=snprintf(line+used,capacity-used,"i%u",index);
 else {
  size_t shown=name_len<96?name_len:96;
  n=snprintf(line+used,capacity-used,"n%.*s",(int)shown,(const char *)name);
 }
 if(n<0||(size_t)n>=capacity-used)return 0;used+=(size_t)n;
 n=snprintf(line+used,capacity-used," type=%u size=%lu bytes=",type,(unsigned long)size);
 if(n<0||(size_t)n>=capacity-used)return 0;used+=(size_t)n;
 static const char hex[]="0123456789abcdef";
 if(capacity-used<2||size>(capacity-used-2)/2)return 0;
 for(size_t i=0;i<size;i++){line[used++]=hex[data[i]>>4];line[used++]=hex[data[i]&15];}
 line[used++]='\n';line[used]=0;return used;
}
#endif
