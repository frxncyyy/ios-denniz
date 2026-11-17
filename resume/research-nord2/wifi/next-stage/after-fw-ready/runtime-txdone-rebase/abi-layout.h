#ifndef NCD_REBASE_ABI_LAYOUT_H
#define NCD_REBASE_ABI_LAYOUT_H
#include <stddef.h>
#define NCD_ABI_FIELDS 21u
void ncd_snapshot_abi(size_t out[NCD_ABI_FIELDS]);
void ncd_canonical_abi(size_t out[NCD_ABI_FIELDS]);
#endif
