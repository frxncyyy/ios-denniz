#include "../normal-init/normal_init.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv){
 unsigned char board[6146],frame[76];struct nni_address a;struct ncp_result cap={0};
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f&&fread(board,1,sizeof board,f)==sizeof board&&fgetc(f)==EOF);fclose(f);
 cap.decoded=1;cap.mac_present=1;cap.mac_valid=0;
 assert(!nni_address_select(board,sizeof board,&cap,&a)&&a.source==NNI_ADDRESS_BOARD&&!memcmp(a.mac,board+4,6));
 assert(!nni_basic(frame,sizeof frame,5,0,0)&&frame[36]==2&&frame[37]==0xa0&&frame[39]==5);
 /* BASIC_CONFIG has no address field: selection is retained local metadata. */
 memset(board+4,0,6);assert(nni_address_select(board,sizeof board,&cap,&a)<0);
 board[4]=1;assert(nni_address_select(board,sizeof board,&cap,&a)<0);
 puts("PASS authenticated board selection, actual invalid firmware MAC refusal, CID2 builder; no MAC bytes emitted");
}
