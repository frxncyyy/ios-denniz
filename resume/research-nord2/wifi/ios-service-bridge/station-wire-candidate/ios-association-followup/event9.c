#include "event9.h"
int swif_event9(void*out,size_t cap,size_t*used,const uint8_t name[16],
 uint64_t generation,uint64_t expected,uint32_t terminal){
 return swia_event9(out,cap,used,name,generation,expected,terminal);
}
