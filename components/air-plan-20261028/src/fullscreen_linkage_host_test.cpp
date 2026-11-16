#include "fullscreen_map.h"
#include <cstring>
int main() {
    fs_mapping mapping{};
    if (!fs_mapping_init(&mapping)) return 1;
    fs_request request{};
    fs_page scratch{UINT64_C(0x882000000), 1};
    request.scratch = &scratch;
    request.scratch_count = 1;
    if (fs_leaf_value(&request, 0) != (scratch.pa | FS_PTE_RW_NX)) return 2;
    if (fs_map(&mapping, &request, nullptr) != FS_RELEASE_STAGED) return 3;
    if (fs_unmap(&mapping) != FS_QUARANTINE) return 4;
    return 0;
}
