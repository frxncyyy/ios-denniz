#ifndef TEGU_SURFACE_DISPLAY_POLICY_H
#define TEGU_SURFACE_DISPLAY_POLICY_H

#include <stdint.h>

#define TEGU_BACKBOARD_DISPLAY_WIDTH 1125u
#define TEGU_BACKBOARD_DISPLAY_HEIGHT 2436u
#define TEGU_BACKBOARD_DISPLAY_FORMAT 0x42475241u
#define TEGU_BACKBOARD_DISPLAY_ROW_BYTES 4608u
#define TEGU_BACKBOARD_DISPLAY_ALLOC_SIZE 11239424u

/* R74's observed linear BackBoard display allocation. Keep this exact so an
 * unrelated ordinary IOSurface cannot silently consume the GPU page pool. */
static inline int tegu_surface_auto_gpu_display(const char *process_name,
    int gpu_property_present, uint32_t width, uint32_t height,
    uint32_t pixel_format, uint16_t bytes_per_element,
    uint32_t bytes_per_row, uint32_t plane_count, uint32_t offset,
    uint32_t allocation_size, uint8_t element_width, uint8_t element_height)
{
    static const char expected[] = "backboardd";
    if (!process_name || gpu_property_present) return 0;
    for (unsigned i = 0; i < sizeof(expected); ++i)
        if (process_name[i] != expected[i]) return 0;
    return width == TEGU_BACKBOARD_DISPLAY_WIDTH &&
        height == TEGU_BACKBOARD_DISPLAY_HEIGHT &&
        pixel_format == TEGU_BACKBOARD_DISPLAY_FORMAT &&
        bytes_per_element == 4 &&
        bytes_per_row == TEGU_BACKBOARD_DISPLAY_ROW_BYTES &&
        plane_count == 0 && offset == 0 &&
        allocation_size == TEGU_BACKBOARD_DISPLAY_ALLOC_SIZE &&
        element_width == 1 && element_height == 1;
}

#endif
