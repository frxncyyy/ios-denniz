/* Shared process query for the compositor gate.  The implementation lives in
 * compute_frontend.m so render can use the same getprogname-based check
 * without duplicating process-name policy. */
#ifndef TEGU_COMPOSITOR_PROCESS_H
#define TEGU_COMPOSITOR_PROCESS_H
#ifdef __cplusplus
extern "C" {
#endif
int tegu_compositor_process_is_backboardd(void);
#ifdef __cplusplus
}
#endif
#endif
