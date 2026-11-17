#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define __KAL_INLINE__ inline
#define OFFSET_OF(t,m) offsetof(t,m)
#define DATA_STRUCT_INSPECTING_ASSERT(x) _Static_assert(x,#x)
#include "CFG_Wifi_File.h"
#include "connfem_api.h"
_Static_assert(offsetof(struct WIFI_CFG_PARAM_STRUCT,ucSupportCoAnt)==243,"coant offset");
_Static_assert(offsetof(struct WIFI_CFG_PARAM_STRUCT,ucTypeID0)==256,"tag start");
_Static_assert(sizeof(struct WIFI_NVRAM_TAG_FORMAT)==3,"NVRAM TLV");
_Static_assert(sizeof(struct connfem_epaelna_pin)==3,"pin ABI");
_Static_assert(sizeof(struct connfem_epaelna_pin_info)==100,"pin info ABI");
_Static_assert(sizeof(struct connfem_epaelna_laa_pin_info)==40,"LAA ABI");
