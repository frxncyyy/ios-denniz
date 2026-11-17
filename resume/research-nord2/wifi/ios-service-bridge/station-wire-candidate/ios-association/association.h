#ifndef PRIVATE_SWIA_ASSOCIATION_H
#define PRIVATE_SWIA_ASSOCIATION_H
#include <stddef.h>
#include <stdint.h>
#define SWIA_BYTES 592u
enum swia_error { SWIA_OK=0, SWIA_INVALID=-1, SWIA_UNSUPPORTED=-2,
                  SWIA_CAPACITY=-3, SWIA_STALE=-4 };
enum swia_reason { SWIA_R_NONE=0,SWIA_R_SHAPE,SWIA_R_AUTH,SWIA_R_CREDENTIAL,
 SWIA_R_RSN,SWIA_R_ASSOC_OPTIONS,SWIA_R_NATIVE_OPTIONS,SWIA_R_CACHE,
 SWIA_R_PRIVATE_OPTIONS,SWIA_R_TRAILING_OPTIONS };
struct swia_join {
 uint8_t ssid_length,ssid[32],has_target,target[6],pmk[32];
 uint16_t auth_lower,auth_upper;
};
/* Safe diagnostic projection only: contains no SSID/address/key/IE bytes. */
struct swia_metadata {uint32_t reason,key_kind,key_length,native_flags;
 uint16_t association_flags;uint8_t association_reason;};
/* Payload parser only; does NOT admit SET20 to a real owner or publish anything.
 * PMK32 only, exact592 input, explicit output capacity. All memory is disjoint.
 * Always clears min(out_capacity,sizeof(*out)) bytes before rejection/success.
 * Caller must zero the original592-byte input as well as decoded output later.
 * Unknown active options fail; known inactive storage/padding is ignored.
 */
int swia_decode_pmk(const void*,size_t,struct swia_join*,size_t,struct swia_metadata*);
void swia_clear(void*,size_t);
/* Raw4-byte GET21 codec. Code0=pending;1=success;2,9,21 are selected failure
 * values, verified through the exact selected consumer's mapping table.
 * Encoding code1 does not establish keys, data, carrier or a real connection.
 * Runtime must bind snapshot to actual owner/client generation before calling.
 */
int swia_result4(void*,size_t,size_t*,uint64_t,uint64_t,uint32_t);
/* EXPERIMENTAL structural no-payload event9 record; not admitted for publication.
 * Reference emits no payload and selected consumer has a GET21 fallback, but
 * that consumer also parses metadata beyond the absent primary payload. Its
 * safe full transport contract remains unresolved. A terminal
 * result must already be committed in the generation-bound native snapshot.
 * 48B event-pipe shape only; never publishes/notifies. This is NOT a proven
 * end-to-end selected-consumer notification interface.
 */
int swia_event9(void*,size_t,size_t*,const uint8_t[16],uint64_t,uint64_t,uint32_t);
/* Base Apple80211Disassociate path is request22/value0/no payload. Extended
 * dictionary path is not handled. No radio teardown is performed by this check.
 */
int swia_disconnect22(uint32_t,uint32_t,size_t);
#endif
