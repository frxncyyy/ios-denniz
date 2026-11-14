/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_server.h -- server side of the datagrams of include/n2vcd_proto.h.  This is the same surface as
 * daemon/n2vcd_srv.h (hw-ip's implementation) and the same semantics, so vcodecd can use either file without
 * touching its protocol code:
 *
 *   socket : SOCK_SEQPACKET UNIX socket (host tests)
 *   relay  : RWVC_MSG_RECV/RWVC_MSG_SEND on minor 0 of /dev/redwood-vcodec (the phone)
 *
 * One client is one accepted socket (socket) or one kernel-assigned client id (relay); the daemon sees a uint32_t
 * id.  Every receive is bounded by the caller's timeout.  A client that goes away is reported exactly once as a
 * close notice: return 0 and *from = its id (the same shape the kernel relay gives, bytes == 0).
 *
 * Host buffers: the P03 client transport stores its DMA buffers in "<socket path>.buf/<decimal handle>".  vcodecd's
 * existing --dma-dir option pointed at that directory makes its file-backed DMA read exactly those files, so no code
 * here has to know about buffers.
 *
 * Darwin has no SOCK_SEQPACKET for AF_UNIX: n2vcd_srv_open_socket() returns NULL with a reason on iOS; the phone
 * always uses the relay.
 */
#ifndef N2VCD_SERVER_H
#define N2VCD_SERVER_H

#include <stddef.h>
#include <stdint.h>

#include "n2vcd_proto.h"

#define N2VCD_SRV_CLIENTS 8

struct n2vcd_srv;

/* Open a listening SOCK_SEQPACKET socket at `path` (unlinked first, removed on close).  NULL on failure; err/errlen
 * gets a one-line reason.  Host only. */
struct n2vcd_srv *n2vcd_srv_open_socket(const char *path, char *err, size_t errlen);

/* Open the relay server side on `dev` ("/dev/redwood-vcodec"), which must be a character device with ABI 1.  NULL
 * on failure; err/errlen gets a one-line reason. */
struct n2vcd_srv *n2vcd_srv_open_relay(const char *dev, char *err, size_t errlen);

/* Wait up to timeout_ms for one datagram.
 *  > 0  bytes received (<= capacity), *from = client id
 *  0    a client closed: *from = its id; no data
 *  -EAGAIN  timeout, nothing happened
 *  -EMSGSIZE a datagram larger than capacity was consumed and dropped; *from = sender (client stays connected)
 *  -errno   transport failure
 */
int n2vcd_srv_recv(struct n2vcd_srv *s, void *buf, uint32_t capacity, uint32_t *from, int timeout_ms);

/* Send one datagram to client `to`.  0, -EAGAIN (this client's queue is full; the datagram is dropped and the client
 * stays connected), -EPIPE (the client is gone and has been dropped) or another -errno. */
int n2vcd_srv_send(struct n2vcd_srv *s, uint32_t to, const void *msg, uint32_t bytes);

/* Relay only: physical address and size of a DMA handle the calling (server) minor may see (RWVC_DMA_INFO).  The
 * socket transport has no physical addresses: -ENOSYS. */
int n2vcd_srv_dma_info(struct n2vcd_srv *s, uint32_t handle, uint32_t *pa, uint32_t *bytes);

void n2vcd_srv_close(struct n2vcd_srv *s);

#endif
