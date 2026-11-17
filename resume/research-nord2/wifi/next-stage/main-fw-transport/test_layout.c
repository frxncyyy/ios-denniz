#include "main_fw_layout.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t bytes) {
    FILE *f = fopen(path, "rb");
    unsigned char *p = malloc(bytes);
    assert(f && p && fread(p, 1, bytes, f) == bytes && fgetc(f) == EOF);
    fclose(f);
    return p;
}
int main(int argc, char **argv) {
    unsigned char frame[36] = {0};
    struct nmt_layout layout;
    unsigned char *main_image, *patch_image;
    assert(argc == 3);
    main_image = read_file(argv[1], 973648u);
    patch_image = read_file(argv[2], 5726u);
    assert(nmt_parse_fixed_layout(main_image, 973648u, patch_image, 5726u, &layout) == 0);
    assert(layout.patch.length == 5696u && layout.patch.space == NMT_AP2WF_WINDOW);
    assert(layout.patch_date.host_physical == 0x8fd7f0d0u);
    assert(layout.main[3].host_physical == 0x8fe1e000u);
    assert(layout.main[4].host_physical == 0x8fec3800u);
    assert(layout.main_entry_override == 0u);
    assert(nmt_parse_fixed_layout(main_image, 973647u, patch_image, 5726u, &layout) != 0);
    patch_image[24] ^= 1u;
    assert(nmt_parse_fixed_layout(main_image, 973648u, patch_image, 5726u, &layout) != 0);
    patch_image[24] ^= 1u;
    main_image[973648u - 36u - 200u + 3u*40u + 24u] ^= 0x80u;
    assert(nmt_parse_fixed_layout(main_image, 973648u, patch_image, 5726u, &layout) != 0);
    main_image[973648u - 36u - 200u + 3u*40u + 24u] ^= 0x80u;
    assert(nmt_parse_fixed_layout(main_image, 973648u, patch_image, 5726u, &layout) == 0);
    frame[26] = 0; frame[27] = 0xe0; frame[28] = 1; frame[29] = 17;
    frame[33] = 0x40;
    assert(nmt_validate_init_result(frame, sizeof(frame), 17, 0x40) == 0);
    assert(nmt_validate_init_result(frame, sizeof(frame)-1, 17, 0x40) != 0);
    assert(nmt_validate_init_result(frame, sizeof(frame), 18, 0x40) != 0);
    assert(nmt_validate_init_result(frame, sizeof(frame), 17, 0x41) != 0);
    frame[32] = 1;
    assert(nmt_validate_init_result(frame, sizeof(frame), 17, 0x40) != 0);
    free(main_image); free(patch_image);
    puts("fixed layout and result framing PASS");
    return 0;
}
