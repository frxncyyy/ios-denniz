/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_srv.h -- server side of the datagrams of include/n2vcd_proto.h: a SOCK_SEQPACKET UNIX socket on the host, the kernel
 * relay (RWVC_MSG_SEND/RECV on minor 0 of /dev/redwood-vcodec) on the phone.  One client is one connection (socket) or one
 * kernel-assigned client id (relay); the daemon only ever sees a uint32_t id.
 *
 * This is hw-ip's implementation for vcodecd.  P03 owns client/n2vcd_server.h with the same job; if that lands with compatible
 * semantics vcodecd can switch to it without touching its protocol code (the four calls below are the whole surface).
 *
 * Every receive is bounded by the caller's timeout.  A client that goes away is reported exactly once as a close notice
 * (return 0, *from set), the same shape the kernel relay gives (bytes == 0).
 */
#ifndef N2VCD_SRV_H
#define N2VCD_SRV_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_proto.h"

#define N2VCD_SRV_CLIENTS 8

struct n2vcd_srv;

/* Open a listening SOCK_SEQPACKET socket at `path` (unlinked first, removed on close).  NULL on failure; err/errlen gets a
 * one-line reason.  Host only (vcodecd refuses it on iOS). */
struct n2vcd_srv *n2vcd_srv_open_socket(const char *path, char *err, size_t errlen);

/* Open the relay server side on `dev` ("/dev/redwood-vcodec"), which must be a character device with ABI 1.  NULL on
 * failure; err/errlen gets a one-line reason. */
struct n2vcd_srv *n2vcd_srv_open_relay(const char *dev, char *err, size_t errlen);

/* Wait up to timeout_ms for one datagram.
 *  > 0  bytes received (<= capacity), *from = client id
 *  0    a client closed: *from = its id; no data
 *  -EAGAIN  timeout, nothing happened
 *  -errno   transport failure
 * A datagram larger than capacity is consumed and reported as -EMSGSIZE (the client is not killed). */
int n2vcd_srv_recv(struct n2vcd_srv *s, void *buf, uint32_t capacity, uint32_t *from, int timeout_ms);

/* Send one datagram to client `to`.  0, -EAGAIN (this client's queue is full; the datagram is dropped and the client stays
 * connected), -EPIPE (the client is gone and has been dropped) or another -errno. */
int n2vcd_srv_send(struct n2vcd_srv *s, uint32_t to, const void *msg, uint32_t bytes);

/* Relay only: physical address and size of a DMA handle the calling (server) minor is allowed to see.  This is the
 * proposed RWVC_DMA_INFO from the board; it compiles to -ENOSYS until include/redwood_vcodec.h defines the ioctl. */
int n2vcd_srv_dma_info(struct n2vcd_srv *s, uint32_t handle, uint32_t *pa, uint32_t *bytes);

void n2vcd_srv_close(struct n2vcd_srv *s);

#endif
