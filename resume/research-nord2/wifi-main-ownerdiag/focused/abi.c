#include "$NORD2_REPO/shim/nord2-xnu/wifi_main_wfdma_native.h"
#include <stddef.h>
_Static_assert(sizeof(struct nwt_state)==1176,"transport");
_Static_assert(sizeof(struct nwi_state)==3040,"initializer");
_Static_assert(sizeof(struct nwfn_native)==7472,"native");
_Static_assert(offsetof(struct nwfn_native,failure_stage)==7440,"appended diagnostics");
_Static_assert(offsetof(struct nwi_state,transport)==1864,"transport offset");
_Static_assert(offsetof(struct nwfn_native,finish_received)==7432,"retained receive offset");
int nord2_ownerdiag_layout_verified;
