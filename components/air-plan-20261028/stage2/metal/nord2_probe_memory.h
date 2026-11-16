/* Diagnostic process only. ABI: matched XNU bsd/sys/kern_memorystatus.h.
 * A finite fatal limit remains enforced; no system or other-process policy
 * changes. Read back both limits before loading the compiler or GPU heap. */
#ifndef NORD2_PROBE_MEMORY_H
#define NORD2_PROBE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define N2_PROBE_MEMORY_MB 256
struct n2_probe_limits {
    int32_t active;
    uint32_t active_attr;
    int32_t inactive;
    uint32_t inactive_attr;
};
_Static_assert(sizeof(struct n2_probe_limits) == 16, "XNU memlimit ABI");
typedef int (*n2_probe_memory_control)(uint32_t, int32_t, uint32_t, void *, size_t);
/* stage: 0 missing API/invalid pid; 1 initial GET; 2 SET; 3 readback;
 * 4 policy mismatch; 5 configured. Never continue the probe on failure. */
static int n2_probe_memory_setup(n2_probe_memory_control control, int32_t pid,
    struct n2_probe_limits *before, struct n2_probe_limits *after, unsigned *stage)
{
    *stage = 0;
    if (!control || pid <= 0) return -1;
    *stage = 1;
    int rc = control(8, pid, 0, before, sizeof(*before));
    if (rc) return rc;
    struct n2_probe_limits desired = {N2_PROBE_MEMORY_MB, 1, N2_PROBE_MEMORY_MB, 1};
    *stage = 2;
    rc = control(7, pid, 0, &desired, sizeof(desired));
    if (rc) return rc;
    *stage = 3;
    rc = control(8, pid, 0, after, sizeof(*after));
    if (rc) return rc;
    *stage = 4;
    if (after->active != N2_PROBE_MEMORY_MB || after->inactive != N2_PROBE_MEMORY_MB ||
        after->active_attr != 1 || after->inactive_attr != 1) return -1;
    *stage = 5;
    return 0;
}
#endif
