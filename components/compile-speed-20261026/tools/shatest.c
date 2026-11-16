#include <stdio.h>
#include "sha256.h"
int main(){struct sha256 s;sha256_init(&s);sha256_update(&s,"abc",3);uint8_t d[32];sha256_final(&s,d);for(int i=0;i<32;i++)printf("%02x",d[i]);printf("\n");}
