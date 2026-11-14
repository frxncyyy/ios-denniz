/* SPDX-License-Identifier: MIT */
/* fake_daemon.h -- test-only daemon-side entry points of the V41 n2vcu stand-in. */
#ifndef V41_FAKE_DAEMON_H
#define V41_FAKE_DAEMON_H
#include <stdint.h>

struct n2vcu;
struct n2a32_mem;

/* Pull one message the way the daemon's VCU_GET_OBJECT does (one queue per IPI instance, selected
 * by ipi_id's slot).  0 on success, -ETIMEDOUT/-EINTR/-EINVAL otherwise. */
int n2vcu_test_get(struct n2vcu *v, int ipi_id, int *out_id, void *buf, uint32_t cap,
                   uint32_t *out_len, unsigned timeout_ms);
/* Deliver one daemon -> AP message the way VCU_SET_OBJECT does; returns the handler's return
 * value (0 = ack, 1 = no ack) or -ENOENT if no handler is registered for that id.  `buf` is
 * modified in place if the handler rewrites it (the kernel copies it back to the daemon). */
int n2vcu_test_set(struct n2vcu *v, int ipi_id, void *buf, uint32_t len);
/* Emulate the *entry* of the daemon's next VCU_GET_OBJECT without fetching a message: vcu_ipi_get
 * sets ipi_done there before it waits (mtk_vcu.c:500), which is what a receiver thread that hands
 * the message to a service thread does before the reply is sent.  Stand-in only. */
void n2vcu_test_reget(struct n2vcu *v, int ipi_id);

struct n2a32_mem *n2vcu_test_mem(struct n2vcu *v);
uint32_t n2vcu_test_dmem(struct n2vcu *v);
void n2vcu_test_counters(struct n2vcu *v, uint64_t *set, uint64_t *get, uint64_t *calls,
                         uint64_t *acks);

#endif
