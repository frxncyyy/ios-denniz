#ifndef PRIVATE_WIFI_SCAN_EXPORT_H
#define PRIVATE_WIFI_SCAN_EXPORT_H
#include "wifi_scan_bss.h"
/* Kernel-only immutable one-boot observation, never an interface readiness or
 * a completion receipt for a later user request. Do not return this pointer
 * through IOExternalMethodArguments. Its pointee remains static until reboot. */
struct nse_view {const struct nsb_scan *scan;uint64_t generation,captured_us;};
/* 0 published observation,1 unavailable,-1 null output. Refusal atomic. */
int nord2_wifi_scan_boot_view(struct nse_view *);
#endif
