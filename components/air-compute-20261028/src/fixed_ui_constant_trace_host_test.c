#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "fixed_ui_constant_trace.h"
int main(void) {
 char line[384];const unsigned char three[]={1,2,255};
 size_t n=tegu_fixed_ui_constant_trace_format(line,sizeof(line),"fixed_vert_lph_spc",7,2,
  0,52,0,0,45,three,sizeof(three));
 assert(n==strlen(line));
 assert(!strcmp(line,"TEGU_FIXED_UI_FC entry=fixed_vert_lph_spc serial=7 ordinal=2 selector=i52 type=45 size=3 bytes=0102ff\n"));
 const unsigned char one[]={0};const unsigned char name[]="fc_generic";
 n=tegu_fixed_ui_constant_trace_format(line,sizeof(line),"fixed_frag_lph_cph",8,0,
  1,0,name,sizeof(name)-1,53,one,sizeof(one));
 assert(n==strlen(line));assert(strstr(line,"selector=nfc_generic type=53 size=1 bytes=00"));
 assert(!tegu_fixed_ui_constant_trace_format(line,24,"fixed_frag_lph_cph",8,0,0,0,0,0,53,one,1));
 puts("fixed UI function-constant trace formatting: PASS");
}
