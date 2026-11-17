#ifndef WIFI_IOS_SCALAR_H
#define WIFI_IOS_SCALAR_H
#include "../../control_contract.h"
/* Driver-owned observed native state, never copied from an IOUC request.
 * SCANNING is the native RUNNING transaction (including preparation/drain),
 * not a queued client ticket or a claim of physical RF emission.
 * This pure encoder authenticates neither the owner nor the observation. */
enum niws_native_state { NIWS_UNAVAILABLE=0, NIWS_IDLE=1, NIWS_SCANNING=2 };
/* GET13 state (INIT0/SCAN1) and GET15 opmode (STA1), raw LE32 response.
 * No carrier/tail output and no GET96/97 guess. Atomic refusal leaves output
 * and used unchanged. The native adapter validates task/generation/lifetime. */
int niws_reply(const struct niw_request *,const char native_name[16],
 enum niws_native_state,void *output,size_t capacity,size_t *used);
#endif
