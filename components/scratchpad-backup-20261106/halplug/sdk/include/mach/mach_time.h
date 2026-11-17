#pragma once
#include <stdint.h>
typedef struct { uint32_t numer, denom; } mach_timebase_info_data_t;
typedef int kern_return_t;
uint64_t mach_absolute_time(void);
kern_return_t mach_timebase_info(mach_timebase_info_data_t *);
