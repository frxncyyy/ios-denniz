#include "abi-layout.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 size_t a[NCD_ABI_FIELDS],b[NCD_ABI_FIELDS];
 ncd_snapshot_abi(a);ncd_canonical_abi(b);
 for(unsigned i=0;i<NCD_ABI_FIELDS;i++)assert(a[i]==b[i]);
 assert(a[0]>=a[11]+sizeof(unsigned));
 printf("PASS snapshot/canonical cross-TU layout: %u fields, nwt=%zu nwi=%zu nwfn=%zu nfp=%zu txdesc=%zu\n",NCD_ABI_FIELDS,a[0],a[12],a[14],a[17],a[4]);
}
