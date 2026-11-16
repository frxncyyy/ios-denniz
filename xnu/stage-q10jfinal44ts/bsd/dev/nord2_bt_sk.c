#include <arm64/sweet_early.h>
#if defined(NORD2)
/*
 * 0721 (bt-20261115): what iOS 16's bluetoothd opens to reach a Bluetooth controller, on top of 0720's controller.
 *
 * Derived from HakiOS (https://github.com/oosexgang/hakios-mt6768): kernel/bsd/dev/redwood_btsk.c (Skywalk kernel pipe)
 * and kernel/bsd/dev/redwood_mtkbt_skel.c (kernel control, nodes, H4 framing, answer rewrites), Copyright (c) 2026 The
 * HakiOS authors, Apple Public Source License 2.0 -- the same licence as the XNU files this port builds. Changes here:
 * one H4 packet per RX slot (the original could join several), vendor commands answered in the kernel instead of being
 * sent to the MediaTek controller (a Broadcom opcode can mean something else to MediaTek firmware, e.g. 0xfc6f/0xfd5b),
 * Broadcom Write_BD_ADDR, MediaTek vendor events dropped, run-time switches, NORD2 BT1 log lines.
 *
 * Verified on the host (disassembly of the phone's iOS 16.7 bluetoothd, 2026-11-15): with no BT_HCI_TRANSPORT the
 * transport comes from the IORegistry service "marconi-bt" or "bluetooth" (transport-encoding, 4 bytes) and defaults to
 * 3 = H4BC; H4BC = PF_SYSTEM kernel control "com.apple.uart.sk.bluetooth" (IOAOSSKYSETCHANNELSPEC / IOAOSSKYGETCHANNELUUID
 * = getsockopt 0x40105412) + os_channel_create on that UUID. bluetoothd takes its address from the service's
 * local-mac-address (FindLocalMacAddress) and runs BlueTool's "boot" script over XPC (com.apple.BlueTool, answered by
 * the n2btd daemon of payload bt1).
 *
 *   kernel control   com.apple.uart.sk.bluetooth, SOCK_STREAM; getsockopt 0x40105412 -> the channel UUID
 *   Skywalk          NEXUS_TYPE_KERNEL_PIPE "nord2_bt": user TX ring -> controller, controller -> user RX ring
 *   nodes            /dev/btwake /dev/btreset /dev/btpoweroff (opened by bluetoothd/BlueTool; nothing behind them)
 */
#include "nord2_bt.h"
#include <kern/locks.h>
#include <kern/kalloc.h>
#include <kern/clock.h>
#include <libkern/libkern.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/ioctl.h>
#include <sys/kern_control.h>
#include <sys/kpi_mbuf.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/termios.h>
#include <sys/ttycom.h>
#include <sys/uio.h>
#include <sys/random.h>
#include <skywalk/os_skywalk_private.h>

extern void IOSleep(unsigned milliseconds);
extern void IODelay(unsigned microseconds);

#define BT1(...) printf("NORD2 BT1: " __VA_ARGS__)

/* ---------------- H4 ---------------- */
static unsigned
n2sk_h4_len(const uint8_t *b, unsigned have)
{
	if (have < 1) return 0;
	switch (b[0]) {
	case 1: return have >= 4 ? 4u + b[3] : 0;
	case 2: return have >= 5 ? 5u + (b[3] | ((unsigned)b[4] << 8)) : 0;
	case 3: return have >= 4 ? 4u + b[3] : 0;
	case 4: return have >= 3 ? 3u + b[2] : 0;
	case 5: return have >= 5 ? 5u + ((b[3] | ((unsigned)b[4] << 8)) & 0x3fff) : 0;
	default: return 0;
	}
}

static void
n2sk_dump(const char *dir, const uint8_t *b, unsigned n)
{
	char t[3 * 32 + 1];
	unsigned k = n < 32 ? n : 32;
	if (!n2bt_trace_left()) return;
	for (unsigned i = 0; i < k; i++) snprintf(t + 3 * i, 4, " %02x", b[i]);
	t[3 * k] = 0;
	BT1("%s %u:%s%s\n", dir, n, t, n > k ? " ..." : "");
}

/* ---------------- queues ---------------- */
static LCK_GRP_DECLARE(n2sk_lck_grp, "nord2-bt-sk");
static LCK_MTX_DECLARE(n2sk_lock, &n2sk_lck_grp);

#define SK_TXQ 16384u
static uint8_t n2sk_txq[SK_TXQ];                  /* what bluetoothd wrote, not yet sent (linear) */
static unsigned n2sk_txq_count;
static uint32_t n2sk_txq_dropped;

#define SK_RXQ 32768u
static uint8_t n2sk_rxq[SK_RXQ];                  /* whole H4 packets for bluetoothd (ring) */
static unsigned n2sk_rxq_head, n2sk_rxq_count;
static uint32_t n2sk_rx_dropped;

static bool
n2sk_txq_put_locked(const uint8_t *b, size_t n)
{
	if (n > SK_TXQ - n2sk_txq_count) {
		n2sk_txq_dropped += (uint32_t)n;
		return false;
	}
	memcpy(n2sk_txq + n2sk_txq_count, b, n);
	n2sk_txq_count += (unsigned)n;
	return true;
}

/* ---------------- Skywalk kernel pipe ---------------- */
static nexus_controller_t n2sk_ncd;
static uuid_t n2sk_dom_uuid, n2sk_prov_uuid, n2sk_nx_uuid;
static bool n2sk_ready;
static uintptr_t n2sk_nx_ctx;
static kern_channel_ring_t n2sk_txring, n2sk_rxring;
static kern_pbufpool_t n2sk_tx_pp, n2sk_rx_pp;
static uint32_t n2sk_said;
#define SAY(...) do { if (n2sk_said < 64) { n2sk_said++; BT1(__VA_ARGS__); } } while (0)

static errno_t n2sk_dom_init(__unused kern_nexus_domain_provider_t d) { return 0; }
static void n2sk_dom_fini(__unused kern_nexus_domain_provider_t d) { }

static errno_t
n2sk_pre_connect(__unused kern_nexus_provider_t p, proc_t pr, kern_nexus_t nexus, nexus_port_t port, kern_channel_t ch, void **ctx)
{
	errno_t e = kern_nexus_get_pbufpool(nexus, &n2sk_tx_pp, &n2sk_rx_pp);
	*ctx = ch;
	SAY("channel pre_connect by pid %d port %u (pools %d)\n", proc_pid(pr), port, e);
	{ static uint32_t said; if (said++ < 12) n2bt_trail("S10h channel pre_connect by pid %d port %u (pools %d)", proc_pid(pr), port, e); }
	return 0;
}
static errno_t
n2sk_connected(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_t c)
{
	static uint32_t said;
	SAY("channel connected\n");
	if (said++ < 4) n2bt_trail("S10h Skywalk channel connected");
	return 0;
}
/* bt14: channel lifecycle breadcrumbs (owner pid) to pair with bluetoothd crash reports */
static void n2sk_pre_disconnect(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_t c)
{
	static uint32_t said;
	SAY("channel pre_disconnect\n");
	if (said++ < 12) n2bt_trail("S10h channel pre_disconnect (pid %d %s)", proc_selfpid(), "caller");
}
static void n2sk_disconnected(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_t c)
{
	static uint32_t said;
	SAY("channel disconnected\n");
	if (said++ < 12) n2bt_trail("S10h channel disconnected (pid %d)", proc_selfpid());
}

static errno_t
n2sk_ring_init(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_t c, kern_channel_ring_t ring,
    boolean_t is_tx, void **rctx)
{
	lck_mtx_lock(&n2sk_lock);
	if (is_tx) n2sk_txring = ring; else n2sk_rxring = ring;
	lck_mtx_unlock(&n2sk_lock);
	*rctx = ring;
	SAY("%s ring made\n", is_tx ? "TX" : "RX");
	return 0;
}

static void
n2sk_ring_fini(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, kern_channel_ring_t ring)
{
	lck_mtx_lock(&n2sk_lock);
	if (ring == n2sk_txring) n2sk_txring = NULL;
	else if (ring == n2sk_rxring) n2sk_rxring = NULL;
	lck_mtx_unlock(&n2sk_lock);
}

static errno_t
n2sk_slot_init(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_ring_t r, channel_slot_t slot,
    __unused uint32_t id, struct kern_slot_prop **prop, void **sctx)
{
	*prop = NULL;
	*sctx = slot;
	return 0;
}
static void n2sk_slot_fini(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, __unused kern_channel_ring_t r,
    __unused channel_slot_t s, __unused uint32_t id) { }

static errno_t
n2sk_sync_tx(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, kern_channel_ring_t ring, __unused uint32_t flags)
{
	kern_channel_slot_t slot = kern_channel_get_next_slot(ring, NULL, NULL), last = NULL;

	while (slot != NULL) {
		kern_packet_t ph = kern_channel_slot_get_packet(ring, slot);
		if (ph != 0) {
			kern_buflet_t buf = kern_packet_get_next_buflet(ph, NULL);
			if (buf != NULL) {
				uint8_t *base = kern_buflet_get_data_address(buf);
				uint16_t len = kern_buflet_get_data_length(buf), off = kern_buflet_get_data_offset(buf);
				if (base != NULL && len > 0) {
					lck_mtx_lock(&n2sk_lock);
					(void)n2sk_txq_put_locked(base + off, len);
					lck_mtx_unlock(&n2sk_lock);
				}
			}
			(void)kern_channel_slot_detach_packet(ring, slot, ph);
			if (n2sk_tx_pp != NULL) kern_pbufpool_free(n2sk_tx_pp, ph);
		}
		last = slot;
		slot = kern_channel_get_next_slot(ring, slot, NULL);
	}
	if (last != NULL) kern_channel_advance_slot(ring, last);
	(void)kern_channel_reclaim(ring);
	return 0;
}

/* length of the packet at the RX ring head (packets are always whole in the ring) */
static unsigned
n2sk_rxq_head_len_locked(void)
{
	uint8_t h[5];
	unsigned k = n2sk_rxq_count < 5 ? n2sk_rxq_count : 5;
	for (unsigned i = 0; i < k; i++) h[i] = n2sk_rxq[(n2sk_rxq_head + i) % SK_RXQ];
	return n2sk_h4_len(h, k);
}

static errno_t
n2sk_sync_rx(__unused kern_nexus_provider_t p, __unused kern_nexus_t n, kern_channel_ring_t ring, __unused uint32_t flags)
{
	kern_channel_slot_t slot, last = NULL;

	(void)kern_channel_reclaim(ring);
	slot = kern_channel_get_next_slot(ring, NULL, NULL);
	lck_mtx_lock(&n2sk_lock);
	while (slot != NULL && n2sk_rxq_count > 0 && n2sk_rx_pp != NULL) {
		kern_packet_t ph = 0;
		kern_buflet_t buf;
		uint8_t *base;
		unsigned room, len = n2sk_rxq_head_len_locked();

		if (len == 0 || len > n2sk_rxq_count) {      /* cannot happen: only whole packets are queued */
			n2sk_rxq_count = 0;
			break;
		}
		if (kern_pbufpool_alloc_nosleep(n2sk_rx_pp, 1, &ph) != 0 || ph == 0) {
			SAY("no packet for an RX slot\n");
			break;
		}
		buf = kern_packet_get_next_buflet(ph, NULL);
		base = buf != NULL ? kern_buflet_get_data_address(buf) : NULL;
		room = buf != NULL ? kern_buflet_get_data_limit(buf) : 0;
		if (base == NULL || room < len) {
			kern_pbufpool_free(n2sk_rx_pp, ph);
			n2sk_rxq_head = (n2sk_rxq_head + len) % SK_RXQ;  /* drop the one that does not fit */
			n2sk_rxq_count -= len;
			n2sk_rx_dropped++;
			continue;
		}
		for (unsigned i = 0; i < len; i++) base[i] = n2sk_rxq[(n2sk_rxq_head + i) % SK_RXQ];
		n2sk_rxq_head = (n2sk_rxq_head + len) % SK_RXQ;
		n2sk_rxq_count -= len;
		(void)kern_buflet_set_data_offset(buf, 0);
		(void)kern_buflet_set_data_length(buf, (uint16_t)len);
		(void)kern_packet_finalize(ph);
		if (kern_channel_slot_attach_packet(ring, slot, ph) != 0) {
			kern_pbufpool_free(n2sk_rx_pp, ph);
			break;
		}
		last = slot;
		slot = kern_channel_get_next_slot(ring, slot, NULL);
	}
	lck_mtx_unlock(&n2sk_lock);
	if (last != NULL) kern_channel_advance_slot(ring, last);
	return 0;
}

static void n2sk_hist_add(char dir, const uint8_t *b, unsigned n);

/* ---------------- bt7: every command since bluetoothd's last HCI_Reset (opcode + status), and non-zero statuses ----------------
 * how: 'c' = sent to the controller, 'k' = answered in the kernel, 'i' = injected by the kernel (answer not handed on).
 * status 0xff = no answer yet. Dumped as "S10z" lines when bluetoothd disconnects (first 6 connections). */
#define N2SK_CLOG 160u
static struct { uint16_t op; uint8_t st; char how; } n2sk_clog[N2SK_CLOG];
static unsigned n2sk_clog_n, n2sk_clog_over;
static uint32_t n2sk_bad_said;

static void
n2sk_clog_add(uint16_t op, char how)
{
	if (op == 0x0c03 && how != 'i') { n2sk_clog_n = 0; n2sk_clog_over = 0; }
	if (n2sk_clog_n >= N2SK_CLOG) { n2sk_clog_over++; return; }
	n2sk_clog[n2sk_clog_n].op = op;
	n2sk_clog[n2sk_clog_n].st = 0xff;
	n2sk_clog[n2sk_clog_n].how = how;
	n2sk_clog_n++;
}

static void
n2sk_clog_status(uint16_t op, uint8_t st)
{
	for (unsigned i = 0; i < n2sk_clog_n; i++) {        /* answers come in order: the oldest unanswered one */
		if (n2sk_clog[i].op == op && n2sk_clog[i].st == 0xff) { n2sk_clog[i].st = st; return; }
	}
}

/* Command Complete / Command Status: opcode + status; false for other packets */
static bool
n2sk_status_of(const uint8_t *b, unsigned n, uint16_t *op, uint8_t *st)
{
	if (n >= 7 && b[0] == 4 && b[1] == 0x0e) { *op = (uint16_t)(b[4] | (b[5] << 8)); *st = b[6]; return *op != 0; }
	if (n >= 7 && b[0] == 4 && b[1] == 0x0f) { *op = (uint16_t)(b[5] | (b[6] << 8)); *st = b[3]; return *op != 0; }
	return false;
}

static void
n2sk_clog_dump(const char *why)
{
	char line[160];
	unsigned k = 0;
	int at = 0;
	for (unsigned i = 0; i < n2sk_clog_n; i++) {
		if (k == 0) at = snprintf(line, sizeof(line), "%s %u-%u:", why, i + 1, i + 12 < n2sk_clog_n ? i + 12 : n2sk_clog_n);
		if (n2sk_clog[i].st == 0xff) at += snprintf(line + at, sizeof(line) - (size_t)at, " %04x%c=-", n2sk_clog[i].op, n2sk_clog[i].how);
		else at += snprintf(line + at, sizeof(line) - (size_t)at, " %04x%c=%02x", n2sk_clog[i].op, n2sk_clog[i].how, n2sk_clog[i].st);
		if (++k == 12 || i + 1 == n2sk_clog_n) { n2bt_trail("%s", line); k = 0; }
	}
	if (n2sk_clog_over) n2bt_trail("%s and %u more commands not listed", why, n2sk_clog_over);
}

/* a whole packet for bluetoothd */
static void
n2sk_deliver(const uint8_t *b, unsigned n)
{
	kern_channel_ring_t ring;

	uint16_t op;
	uint8_t st;

	n2sk_dump("in ", b, n);
	lck_mtx_lock(&n2sk_lock);
	n2sk_hist_add('i', b, n);
	if (n2sk_status_of(b, n, &op, &st)) {
		n2sk_clog_status(op, st);
		if (st != 0 && n2sk_bad_said < 40) {
			n2sk_bad_said++;
			lck_mtx_unlock(&n2sk_lock);
			n2bt_trail("S10y bluetoothd gets status %#04x for %#06x (%s)", st, op, b[1] == 0x0e ? "Command Complete" : "Command Status");
			lck_mtx_lock(&n2sk_lock);
		}
	}
	if (!n2sk_ready || n > SK_RXQ - n2sk_rxq_count) {
		n2sk_rx_dropped++;
		lck_mtx_unlock(&n2sk_lock);
		return;
	}
	for (unsigned i = 0; i < n; i++) n2sk_rxq[(n2sk_rxq_head + n2sk_rxq_count + i) % SK_RXQ] = b[i];
	n2sk_rxq_count += n;
	ring = n2sk_rxring;
	lck_mtx_unlock(&n2sk_lock);
	if (ring != NULL) (void)kern_channel_notify(ring, 0);
}

static int
n2sk_nexus_init(void)
{
	nexus_attr_t nxa = NULL;
	errno_t e;
	struct kern_nexus_domain_provider_init dom = {
		.nxdpi_version = KERN_NEXUS_DOMAIN_PROVIDER_CURRENT_VERSION, .nxdpi_flags = 0,
		.nxdpi_init = n2sk_dom_init, .nxdpi_fini = n2sk_dom_fini,
	};
	struct kern_nexus_provider_init prov = {
		.nxpi_version = KERN_NEXUS_DOMAIN_PROVIDER_CURRENT_VERSION, .nxpi_flags = NXPIF_VIRTUAL_DEVICE,
		.nxpi_pre_connect = n2sk_pre_connect, .nxpi_connected = n2sk_connected, .nxpi_pre_disconnect = n2sk_pre_disconnect,
		.nxpi_disconnected = n2sk_disconnected, .nxpi_ring_init = n2sk_ring_init, .nxpi_ring_fini = n2sk_ring_fini,
		.nxpi_slot_init = n2sk_slot_init, .nxpi_slot_fini = n2sk_slot_fini, .nxpi_sync_tx = n2sk_sync_tx, .nxpi_sync_rx = n2sk_sync_rx,
		.nxpi_tx_doorbell = NULL,
	};
	struct kern_nexus_init nxi = { .nxi_version = KERN_NEXUS_CURRENT_VERSION, .nxi_flags = 0, .nxi_tx_pbufpool = NULL, .nxi_rx_pbufpool = NULL };

	read_random(&n2sk_nx_ctx, sizeof(n2sk_nx_ctx));
	if ((e = kern_nexus_register_domain_provider(NEXUS_TYPE_KERNEL_PIPE, (const uint8_t *)"nord2_bt", &dom, sizeof(dom), &n2sk_dom_uuid)) != 0) {
		BT1("skywalk domain provider: %d\n", e); return e;
	}
	if ((e = kern_nexus_controller_create(&n2sk_ncd)) != 0) { BT1("skywalk controller: %d\n", e); return e; }
	if ((e = kern_nexus_attr_create(&nxa)) != 0) { BT1("skywalk attr: %d\n", e); return e; }
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_ANONYMOUS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_TX_RINGS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_RX_RINGS, 1);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_TX_SLOTS, 64);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_RX_SLOTS, 128);
	(void)kern_nexus_attr_set(nxa, NEXUS_ATTR_SLOT_BUF_SIZE, 2048);
	e = kern_nexus_controller_register_provider(n2sk_ncd, n2sk_dom_uuid, (const uint8_t *)"com.apple.nexus.nord2_bt", &prov, sizeof(prov), nxa, &n2sk_prov_uuid);
	kern_nexus_attr_destroy(nxa);
	if (e != 0) { BT1("skywalk provider: %d\n", e); return e; }
	if ((e = kern_nexus_controller_alloc_provider_instance(n2sk_ncd, n2sk_prov_uuid, (const void *)n2sk_nx_ctx, NULL, &n2sk_nx_uuid, &nxi)) != 0) {
		BT1("skywalk instance: %d\n", e); return e;
	}
	lck_mtx_lock(&n2sk_lock);
	n2sk_ready = true;
	lck_mtx_unlock(&n2sk_lock);
	{
		uuid_string_t t;
		uuid_unparse_upper(n2sk_nx_uuid, t);
		BT1("Skywalk kernel pipe %s up\n", t);
	}
	return 0;
}

/* ---------------- bt5: what bluetoothd's H4BC transport does on the control socket ----------------
 * [V disassembly of the phone's bluetoothd 16.7, transport open at 0x100066bf8] socket(PF_SYSTEM, SOCK_STREAM, SYSPROTO_CONTROL),
 * ioctl CTLIOCGINFO, connect, setsockopt(0x800c5414, 12 bytes: channel spec), getsockopt(0x40105412, 16: channel UUID),
 * os_channel_create + kqueue, then the UART-style setup on the SAME socket: getsockopt(0x40487413 = TIOCGETA, struct termios,
 * 72 bytes; return value not checked), cfmakeraw, cfsetspeed(baud), flow-control bits, setsockopt(0x80487414 = TIOCSETA, 72).
 * bt4 answered TIOCGETA with nothing (bluetoothd then set a zeroed termios); bt5 answers a real termios (CS8|CREAD|CLOCAL,
 * 3000000 baud) and returns what bluetoothd last set. Every option is logged once per connection (+ breadcrumb, with the result
 * given back), and when bluetoothd disconnects the last 12 HCI packets of that connection are written as a breadcrumb. */
struct n2sk_termios {                     /* arm64 user struct termios: tcflag_t/speed_t are unsigned long, NCCS 20 */
	uint64_t c_iflag, c_oflag, c_cflag, c_lflag;
	uint8_t c_cc[20];
	uint8_t pad[4];
	uint64_t c_ispeed, c_ospeed;
};
_Static_assert(sizeof(struct n2sk_termios) == 72, "termios layout");
static struct n2sk_termios n2sk_tio = { .c_cflag = 0x300 | 0x800 | 0x8000, .c_ispeed = 3000000, .c_ospeed = 3000000 }; /* CS8 CREAD CLOCAL */
static uint64_t n2sk_conn_at;
static uint32_t n2sk_conn_count, n2sk_opt_said;

#define N2SK_HIST 12
static struct { char dir; uint8_t len; uint8_t b[8]; } n2sk_hist[N2SK_HIST];
static unsigned n2sk_hist_pos, n2sk_hist_n;

static void
n2sk_hist_add(char dir, const uint8_t *b, unsigned n)
{
	unsigned i = n2sk_hist_pos;
	n2sk_hist[i].dir = dir;
	n2sk_hist[i].len = (uint8_t)(n > 255 ? 255 : n);
	for (unsigned k = 0; k < 8; k++) n2sk_hist[i].b[k] = k < n ? b[k] : 0;
	n2sk_hist_pos = (i + 1) % N2SK_HIST;
	if (n2sk_hist_n < N2SK_HIST) n2sk_hist_n++;
}

static void
n2sk_hist_dump(const char *why)
{
	char line[128];
	unsigned n = n2sk_hist_n, start = (n2sk_hist_pos + N2SK_HIST - n) % N2SK_HIST;
	for (unsigned j = 0; j < n; j++) {
		unsigned i = (start + j) % N2SK_HIST;
		snprintf(line, sizeof(line), "%s: %s %u: %02x %02x %02x %02x %02x %02x %02x %02x", why, n2sk_hist[i].dir == 'o' ? "out" : "in ",
		    n2sk_hist[i].len, n2sk_hist[i].b[0], n2sk_hist[i].b[1], n2sk_hist[i].b[2], n2sk_hist[i].b[3], n2sk_hist[i].b[4],
		    n2sk_hist[i].b[5], n2sk_hist[i].b[6], n2sk_hist[i].b[7]);
		n2bt_trail("%s", line);
	}
}

/* ---------------- kernel control ---------------- */
static kern_ctl_ref n2sk_ctl_ref;

static errno_t
n2sk_ctl_connect(__unused kern_ctl_ref r, struct sockaddr_ctl *sac, __unused void **ui)
{
	char name[32] = "";
	proc_selfname(name, sizeof(name));
	BT1("control connected by %s (pid %d) unit %u\n", name, proc_selfpid(), sac->sc_unit);
	lck_mtx_lock(&n2sk_lock);
	n2sk_conn_at = mach_absolute_time();
	n2sk_conn_count++;
	n2sk_opt_said = 0;
	n2sk_hist_n = 0;
	lck_mtx_unlock(&n2sk_lock);
	{ static uint32_t said; if (said++ < 4) n2bt_trail("S10f control connected by %s (pid %d)", name, proc_selfpid()); }
	return 0;
}
static errno_t
n2sk_ctl_disconnect(__unused kern_ctl_ref r, u_int32_t unit, __unused void *ui)
{
	uint64_t ns = 0;
	absolutetime_to_nanoseconds(mach_absolute_time() - n2sk_conn_at, &ns);
	BT1("control unit %u disconnected\n", unit);
	if (n2sk_conn_count <= 6) {
		n2bt_trail("S10x bluetoothd disconnected after %llu ms (connection %u); its last HCI packets:", ns / 1000000ull, n2sk_conn_count);
		lck_mtx_lock(&n2sk_lock);
		n2sk_hist_dump("S10x");
		n2sk_clog_dump("S10z");
		lck_mtx_unlock(&n2sk_lock);
	}
	return 0;
}

static errno_t
n2sk_ctl_send(__unused kern_ctl_ref r, __unused u_int32_t unit, __unused void *ui, mbuf_t m, __unused int flags)
{
	errno_t e = 0;
	lck_mtx_lock(&n2sk_lock);
	for (mbuf_t part = m; part != NULL; part = mbuf_next(part)) {
		if (!n2sk_txq_put_locked(mbuf_data(part), mbuf_len(part))) { e = ENOBUFS; break; }
	}
	lck_mtx_unlock(&n2sk_lock);
	mbuf_freem(m);
	return e;
}

static errno_t
n2sk_ctl_setopt(__unused kern_ctl_ref r, __unused u_int32_t unit, __unused void *ui, int opt, void *data, size_t len)
{
	const uint8_t *d = data;
	unsigned o = (unsigned)opt;
	errno_t e = 0;
	const char *what = "taken";

	if ((o == 0x80487414u || o == 0x80487415u || o == 0x80487416u) && data != NULL && len == sizeof(n2sk_tio)) {
		memcpy(&n2sk_tio, data, sizeof(n2sk_tio));          /* TIOCSETA/W/F: kept, given back by TIOCGETA */
		what = "termios stored";
	}
	if (n2sk_opt_said++ < 16) {
		BT1("control setsockopt %#x (%zu bytes): %s\n", o, len, what);
		n2bt_trail("S10o setsockopt %#x len %zu -> %d (%s) data %02x %02x %02x %02x %02x %02x %02x %02x cflag %#llx speed %llu", o, len, e, what,
		    len > 0 && d ? d[0] : 0, len > 1 && d ? d[1] : 0, len > 2 && d ? d[2] : 0, len > 3 && d ? d[3] : 0, len > 4 && d ? d[4] : 0,
		    len > 5 && d ? d[5] : 0, len > 6 && d ? d[6] : 0, len > 7 && d ? d[7] : 0,
		    (unsigned long long)n2sk_tio.c_cflag, (unsigned long long)n2sk_tio.c_ospeed);
	}
	return e;
}

static errno_t
n2sk_ctl_getopt(__unused kern_ctl_ref r, __unused u_int32_t unit, __unused void *ui, int opt, void *data, size_t *len)
{
	unsigned o = (unsigned)opt;
	size_t room = *len;
	const char *what;

	if (o == 0x40105412u && data != NULL && room >= sizeof(uuid_t) && n2sk_ready) {
		memcpy(data, n2sk_nx_uuid, sizeof(uuid_t));
		*len = sizeof(uuid_t);
		what = "channel UUID";
	} else if (o == 0x40487413u && data != NULL && room >= sizeof(n2sk_tio)) {   /* TIOCGETA */
		memcpy(data, &n2sk_tio, sizeof(n2sk_tio));
		*len = sizeof(n2sk_tio);
		what = "termios";
	} else {
		*len = 0;
		what = "nothing (unknown option)";
	}
	BT1("control getsockopt %#x (room %zu): %s\n", o, room, what);
	if (n2sk_opt_said++ < 16) n2bt_trail("S10g getsockopt %#x room %zu -> 0, %zu bytes (%s)", o, room, *len, what);
	return 0;
}

static int
n2sk_ctl_register(void)
{
	struct kern_ctl_reg reg;
	errno_t e;
	bzero(&reg, sizeof(reg));
	strlcpy(reg.ctl_name, "com.apple.uart.sk.bluetooth", sizeof(reg.ctl_name));
	reg.ctl_flags = CTL_FLAG_REG_SOCK_STREAM;
	reg.ctl_sendsize = 65536;
	reg.ctl_recvsize = 65536;
	reg.ctl_connect = n2sk_ctl_connect;
	reg.ctl_disconnect = n2sk_ctl_disconnect;
	reg.ctl_send = n2sk_ctl_send;
	reg.ctl_setopt = n2sk_ctl_setopt;
	reg.ctl_getopt = n2sk_ctl_getopt;
	e = ctl_register(&reg, &n2sk_ctl_ref);
	BT1("kernel control com.apple.uart.sk.bluetooth %s (%d)\n", e == 0 ? "registered" : "NOT registered", e);
	return e;
}

/* ---------------- nodes ---------------- */
static int
n2sk_node_open(dev_t dev, int flags, __unused int type, __unused struct proc *p)
{
	static uint32_t said;
	char name[32] = "";
	if (said++ < 8) { proc_selfname(name, sizeof(name)); BT1("node %u opened by %s flags %#x\n", minor(dev), name, flags); }
	return 0;
}
static int n2sk_node_close(__unused dev_t d, __unused int f, __unused int t, __unused struct proc *p) { return 0; }
static int n2sk_node_read(__unused dev_t d, __unused struct uio *u, __unused int f) { return 0; }
static int n2sk_node_write(__unused dev_t d, struct uio *uio, __unused int f) { uio_setresid(uio, 0); return 0; }

static int
n2sk_node_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int flag, __unused struct proc *p)
{
	switch (cmd) {
	case TIOCGETA: {
		struct termios *t = (struct termios *)(void *)data;
		bzero(t, sizeof(*t));
		t->c_cflag = CS8 | CREAD | CLOCAL | HUPCL;
		t->c_ispeed = t->c_ospeed = 3000000;
		return 0;
	}
	case TIOCSETA: case TIOCSETAW: case TIOCSETAF: case TIOCEXCL: case TIOCNXCL: case TIOCSDTR: case TIOCCDTR:
	case TIOCMBIS: case TIOCMBIC: case TIOCMSET: case TIOCFLUSH: case TIOCSBRK: case TIOCCBRK: case TIOCDRAIN:
	case FIONBIO: case FIOASYNC:
		return 0;
	case TIOCMGET:
		*(int *)(void *)data = TIOCM_CTS | TIOCM_DSR | TIOCM_CAR | TIOCM_RTS | TIOCM_DTR;
		return 0;
	case FIONREAD:
		*(int *)(void *)data = 0;
		return 0;
	default:
		return ENOTTY;
	}
}
static int n2sk_node_select(__unused dev_t d, __unused int w, __unused void *wql, __unused struct proc *p) { return 1; }

static const struct cdevsw n2sk_cdevsw = {
	.d_open = n2sk_node_open, .d_close = n2sk_node_close, .d_read = n2sk_node_read, .d_write = n2sk_node_write,
	.d_ioctl = n2sk_node_ioctl, .d_stop = eno_stop, .d_reset = eno_reset, .d_select = n2sk_node_select,
	.d_mmap = eno_mmap, .d_strategy = eno_strat, .d_type = 0
};

static int
n2sk_nodes(void)
{
	static const char *const names[] = { "btwake", "btreset", "btpoweroff" };
	int major = cdevsw_add(-1, &n2sk_cdevsw);
	if (major < 0) { BT1("cdevsw_add failed (%d)\n", major); return ENXIO; }
	for (unsigned i = 0; i < 3; i++) {
		if (devfs_make_node(makedev(major, i), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "%s", names[i]) == NULL) {
			BT1("/dev/%s not made\n", names[i]);
			return ENXIO;
		}
	}
	BT1("/dev/btwake /dev/btreset /dev/btpoweroff made (major %d)\n", major);
	return 0;
}

/* ---------------- answer rewrites ---------------- */
static uint8_t n2sk_ev[300];

/* a command bluetoothd wrote: true when answered here (not for the controller) */
static bool
n2sk_answer_here(const uint8_t *c, unsigned n)
{
	uint32_t q = n2bt_quirks();
	uint16_t op;

	if (n < 4 || c[0] != 1) return false;
	op = (uint16_t)(c[1] | (c[2] << 8));
	if ((op == 0x0c5b || op == 0x0c5a) && (q & N2BT_Q_ERRDATA)) {
		/* bt5 [V phone unified log, bt22a]: bluetoothd's HCI init ends with OI_HCICMD_WriteDefaultErroneousDataReporting; it
		 * failed locally (status=628, never on the wire) because the MediaTek mask has no bit for it -> "HCI initialization
		 * failed", restart reason 108, every ~10 s. The mask now advertises it (n2sk_rewrite) and the command is answered
		 * here: Write -> status 0; Read -> status 0 + mode 0 (erroneous data reporting disabled, the controller's behaviour). */
		n2sk_ev[0] = 4; n2sk_ev[1] = 0x0e; n2sk_ev[2] = op == 0x0c5a ? 5 : 4; n2sk_ev[3] = 1; n2sk_ev[4] = c[1]; n2sk_ev[5] = c[2]; n2sk_ev[6] = 0;
		n2sk_ev[7] = 0;
		n2sk_deliver(n2sk_ev, op == 0x0c5a ? 8 : 7);
		return true;
	}
	if ((op == 0x0c13 || op == 0x0c52) && (q & N2BT_Q_LONGLOCAL)) {
		/* bt6 [V phone, bt23a]: the controller answered every command up to Write Local Name (252 bytes) and then nothing
		 * at all (not that, not Write EIR, not the next bluetoothd's HCI_Reset: 20.7 s timeouts, restart). Until the cause of
		 * the long-packet stall is known, these two host-only settings are answered here (status 0) and not sent: the name
		 * and EIR other devices see stay the controller's defaults. kern.n2bt_quirks without 0x40 sends them again (paced). */
		static uint32_t said;
		if (said++ < 4) n2bt_trail("S10l %s (%u bytes) answered in the kernel, not sent", op == 0x0c13 ? "Write Local Name" : "Write EIR", n);
		n2sk_ev[0] = 4; n2sk_ev[1] = 0x0e; n2sk_ev[2] = 4; n2sk_ev[3] = 1; n2sk_ev[4] = c[1]; n2sk_ev[5] = c[2]; n2sk_ev[6] = 0;
		n2sk_deliver(n2sk_ev, 7);
		return true;
	}
	if (op == 0x204a && (q & N2BT_Q_NOEXT)) {
		/* bt8 [V phone, live capture]: after bluetoothd's init the controller refused every legacy LE scan command with 0x0c
		 * Command Disallowed, even our Scan Enable(0). The only "extended advertising" command of that init is LE Read Periodic
		 * Advertiser List Size (0x204a); the Core spec (Vol 4 Part E 3.1.1) lets a controller refuse legacy advertising/scanning
		 * commands with 0x0c once the host used one [I: that this MediaTek firmware does so]. Answered here: status 0, size 0x10
		 * (the controller's own answer in bt25a), and never sent. */
		static uint32_t said;
		if (said++ < 4) n2bt_trail("S10n LE Read Periodic Advertiser List Size answered in the kernel (keeps the controller in legacy LE mode)");
		n2sk_ev[0] = 4; n2sk_ev[1] = 0x0e; n2sk_ev[2] = 5; n2sk_ev[3] = 1; n2sk_ev[4] = c[1]; n2sk_ev[5] = c[2]; n2sk_ev[6] = 0; n2sk_ev[7] = 0x10;
		n2sk_deliver(n2sk_ev, 8);
		return true;
	}
	if ((op == 0x200b || op == 0x200c) && (q & N2BT_Q_LESCAN_LOCAL)) {
		static uint32_t said;
		if (said++ < 4) n2bt_trail("S10n %s answered in the kernel, not sent (kern.n2bt_quirks 0x200)", op == 0x200b ? "LE Set Scan Parameters" : "LE Set Scan Enable");
		n2sk_ev[0] = 4; n2sk_ev[1] = 0x0e; n2sk_ev[2] = 4; n2sk_ev[3] = 1; n2sk_ev[4] = c[1]; n2sk_ev[5] = c[2]; n2sk_ev[6] = 0;
		n2sk_deliver(n2sk_ev, 7);
		return true;
	}
	if ((op >> 10) != 0x3f) return false;
	if (op == 0xfc01 && (q & N2BT_Q_BCM_BDADDR)) {
		static uint32_t said;
		if (n >= 10 && said++ < 4) BT1("Broadcom Write_BD_ADDR %02x:%02x:%02x:%02x:%02x:%02x answered here (controller keeps its own)\n", c[9], c[8], c[7], c[6], c[5], c[4]);
	} else if (!(q & N2BT_Q_VSC_DONE)) {
		return false;
	}
	{
		/* HakiOS findings (MT6631 + iOS 16.5 bluetoothd): the data each vendor command's parser reads after the status */
		uint8_t sub = n >= 5 ? c[4] : 0;
		bool antenna = op == 0xfce2 && (sub == 0x15 || sub == 0x36);
		bool cal = op == 0xfd1b;
		unsigned extra = cal ? 64 : antenna ? 16 : op == 0xfd48 ? 1 : 0;
		/* bt12 [V phone bt31a crash reports + disassembly of bluetoothd 20H392]: abort() in bcm_vsc.c:138
		 * _BCM_ProfilingStatsEventParser, "ByteStream_NumReadBytesAvail(*pBs) >= 2": the reply to 0xfce2 sub 0x0d (sent ~20 s
		 * into a headset connection, bluetoothd +0x1282b4) is parsed as four uint16 and we gave none. bluetoothd's BCM
		 * Command Complete parsers only check ">= n" (the "== 0" checks are in the vendor-event handler and in the ACI
		 * (Apple chip) file), so a zero tail is safe: >= 16 bytes for every vendor command (sub 0x2d needs 4, 0x05/0x0a 1). */
		if ((q & N2BT_Q_VSC_PAD) && extra < 16) extra = 16;
		/* bt13 [V phone bt32a]: the blanket pad stalled bluetoothd's init at 0xfce9 (15.6 s timeout, every connection): its
		 * handlers read the reply by structure. So, by default, data only for the 0xfce2 sub-opcodes whose parsers need it
		 * [V disassembly: +0x123ab4 four u16, +0x12652c >= 4 bytes, +0x12359c >= 1 byte]; every other answer stays as in bt31a. */
		if ((q & N2BT_Q_VSC_TARGET) && op == 0xfce2 && !antenna) {
			unsigned need = sub == 0x0d ? 8 : sub == 0x2d ? 4 : (sub == 0x05 || sub == 0x0a) ? 1 : 0;
			if (extra < need) extra = need;
		}
		n2sk_ev[0] = 4; n2sk_ev[1] = 0x0e; n2sk_ev[2] = (uint8_t)(4 + extra); n2sk_ev[3] = 1; n2sk_ev[4] = c[1]; n2sk_ev[5] = c[2]; n2sk_ev[6] = 0;
		memset(n2sk_ev + 7, 0, extra);
		if (cal) n2sk_ev[7] = 0x51;
		n2sk_deliver(n2sk_ev, 7 + extra);
	}
	return true;
}

/* a packet from the controller: new length, 0 = drop */
static unsigned
n2sk_rewrite(uint8_t *p, unsigned n)
{
	static const uint8_t le_cmds[4] = { 0xf7, 0xff, 0xff, 0x7f };
	uint32_t q = n2bt_quirks();

	if (n >= 3 && p[0] == 4 && (p[1] == 0xe4 || p[1] == 0xff) && (q & N2BT_Q_DROP_MTK_EVT)) {
		static uint32_t said;
		if (said++ < 8) BT1("MediaTek vendor event %#x (%u bytes) not handed on\n", p[1], n);
		return 0;
	}
	if (n == 7 && (q & N2BT_Q_ERRDATA) && memcmp(p, "\x04\x0f\x04\x01\x01\x5b\x0c", 7) == 0) {
		p[1] = 0x0e; p[2] = 4; p[3] = 1; p[4] = 0x5b; p[5] = 0x0c; p[6] = 0;
		return 7;
	}
	if ((q & N2BT_Q_LE_CMDS) && n >= 7 + 64 && p[0] == 4 && p[1] == 0x0e && p[2] == 0x44 && p[4] == 0x02 && p[5] == 0x10 && p[6] == 0) {
		for (unsigned i = 0; i < 4; i++) p[7 + 25 + i] |= le_cmds[i];
		p[7 + 24] |= 0x60;
		p[7 + 18] |= 0x0c;                       /* bt5: Read/Write Default Erroneous Data Reporting (answered in the kernel) */
	}
	return n;
}

/* ---------------- bt4: the per-phone address survives bluetoothd's HCI_Reset ----------------
 * [I] whether a MediaTek controller keeps a 0xfc1a address across HCI_Reset is not documented; bluetoothd resets the controller
 * when it starts and on every power cycle. After the controller's Command Complete for HCI_Reset (0x0c03) the bridge holds that
 * answer, sends 0xfc1a with the address in use, swallows its answer (Command Complete / Command Status for 0xfc1a), and only then
 * hands the Reset answer to bluetoothd (so bluetoothd cannot have a command in flight meanwhile; 1 s timeout). */
static bool
n2sk_is_cc(const uint8_t *b, unsigned n, uint16_t op)
{
	return n >= 7 && b[0] == 4 && b[1] == 0x0e && b[4] == (uint8_t)op && b[5] == (uint8_t)(op >> 8);
}

static bool
n2sk_is_cs(const uint8_t *b, unsigned n, uint16_t op)
{
	return n >= 7 && b[0] == 4 && b[1] == 0x0f && b[5] == (uint8_t)op && b[6] == (uint8_t)(op >> 8);
}

/* ---------------- bt8: runtime breadcrumbs (pairing / profiles) ----------------
 * Every controller packet and every bluetoothd packet is counted; the events that matter for connections and pairing get an
 * "S11e" line, link-control/link-policy commands an "S11c" line, the first packet of each new event code an "S11e first" line,
 * the first ACL in each direction after a connection an "S11a" line, and every 15 s of traffic an "S11s" counter line. They go
 * to the console and to the persistent ring (the last 41 lines survive a reset). Parsing never reads past the packet. */
struct n2sk_rt {
	uint32_t acl_rx, acl_rx_b, acl_rx_max, acl_tx, acl_tx_b, acl_tx_max, sco_rx, sco_tx, iso, ev, nocp, inq, cmd;
};
static struct n2sk_rt n2sk_rt;

static void
n2sk_count(char dir, const uint8_t *p, unsigned n, struct n2sk_rt *r)
{
	if (n < 1) return;
	switch (p[0]) {
	case 1: r->cmd++; break;
	case 2:
		if (dir == 'i') { r->acl_rx++; r->acl_rx_b += n; if (n > r->acl_rx_max) r->acl_rx_max = n; }
		else { r->acl_tx++; r->acl_tx_b += n; if (n > r->acl_tx_max) r->acl_tx_max = n; }
		break;
	case 3: if (dir == 'i') r->sco_rx++; else r->sco_tx++; break;
	case 4:
		r->ev++;
		if (n >= 2 && p[1] == 0x13) r->nocp++;
		if (n >= 2 && (p[1] == 0x02 || p[1] == 0x22 || p[1] == 0x2f)) r->inq++;
		break;
	case 5: r->iso++; break;
	default: break;
	}
}

/* an event worth a line: its text (length > 0), else 0 */
static int
n2sk_event_text(const uint8_t *p, unsigned n, char *t, size_t room)
{
	const uint8_t *q;
	unsigned ql;
	uint8_t ev;

	if (n < 3 || p[0] != 4 || room < 2) return 0;
	ev = p[1];
	q = p + 3;
	ql = n - 3;
	if (ql > p[2]) ql = p[2];
#define N2SK_NEED(k) do { if (ql < (unsigned)(k)) return snprintf(t, room, "S11e event %#04x too short (%u bytes)", ev, ql); } while (0)
#define N2SK_H(i) ((unsigned)(q[i] | (q[(i) + 1] << 8)) & 0xfffu)
	switch (ev) {
	case 0x01: N2SK_NEED(1); return snprintf(t, room, "S11e inquiry complete st %#x", q[0]);
	case 0x03: N2SK_NEED(11); return snprintf(t, room, "S11e conn complete st %#x h %#x ..:%02x:%02x link %u enc %u", q[0], N2SK_H(1), q[4], q[3], q[9], q[10]);
	case 0x04: N2SK_NEED(10); return snprintf(t, room, "S11e conn request ..:%02x:%02x cod %02x%02x%02x link %u", q[1], q[0], q[8], q[7], q[6], q[9]);
	case 0x05: N2SK_NEED(4); return snprintf(t, room, "S11e disconn complete st %#x h %#x reason %#x", q[0], N2SK_H(1), q[3]);
	case 0x06: N2SK_NEED(3); return snprintf(t, room, "S11e auth complete st %#x h %#x", q[0], N2SK_H(1));
	case 0x07: N2SK_NEED(7); return snprintf(t, room, "S11e remote name complete st %#x ..:%02x:%02x", q[0], q[2], q[1]);
	case 0x08: N2SK_NEED(4); return snprintf(t, room, "S11e encryption change st %#x h %#x on %u", q[0], N2SK_H(1), q[3]);
	case 0x0b: N2SK_NEED(3); return snprintf(t, room, "S11e remote features st %#x h %#x", q[0], N2SK_H(1));
	case 0x0c: N2SK_NEED(8); return snprintf(t, room, "S11e remote version st %#x h %#x ver %u mfr %#x", q[0], N2SK_H(1), q[3], q[4] | (q[5] << 8));
	case 0x10: N2SK_NEED(1); return snprintf(t, room, "S11e HARDWARE ERROR code %#x", q[0]);
	case 0x12: N2SK_NEED(8); return snprintf(t, room, "S11e role change st %#x ..:%02x:%02x role %u", q[0], q[2], q[1], q[7]);
	case 0x14: N2SK_NEED(6); return snprintf(t, room, "S11e mode change st %#x h %#x mode %u interval %u", q[0], N2SK_H(1), q[3], q[4] | (q[5] << 8));
	case 0x17: N2SK_NEED(6); return snprintf(t, room, "S11e link key request ..:%02x:%02x", q[1], q[0]);
	case 0x18: N2SK_NEED(23); return snprintf(t, room, "S11e link key notification ..:%02x:%02x type %u", q[1], q[0], q[22]);
	case 0x1a: N2SK_NEED(1); return snprintf(t, room, "S11e DATA BUFFER OVERFLOW link %u", q[0]);
	case 0x2c: N2SK_NEED(17); return snprintf(t, room, "S11e sync conn complete st %#x h %#x link %u rx %u tx %u air %u", q[0], N2SK_H(1), q[9],
		    q[12] | (q[13] << 8), q[14] | (q[15] << 8), q[16]);
	case 0x2d: N2SK_NEED(9); return snprintf(t, room, "S11e sync conn changed st %#x h %#x", q[0], N2SK_H(1));
	case 0x30: N2SK_NEED(3); return snprintf(t, room, "S11e encryption key refresh st %#x h %#x", q[0], N2SK_H(1));
	case 0x31: N2SK_NEED(6); return snprintf(t, room, "S11e io capability request ..:%02x:%02x", q[1], q[0]);
	case 0x32: N2SK_NEED(9); return snprintf(t, room, "S11e io capability response ..:%02x:%02x io %u oob %u auth %u", q[1], q[0], q[6], q[7], q[8]);
	case 0x33: N2SK_NEED(6); return snprintf(t, room, "S11e user confirmation request ..:%02x:%02x", q[1], q[0]);
	case 0x34: N2SK_NEED(6); return snprintf(t, room, "S11e passkey request ..:%02x:%02x", q[1], q[0]);
	case 0x36: N2SK_NEED(7); return snprintf(t, room, "S11e simple pairing complete st %#x ..:%02x:%02x", q[0], q[2], q[1]);
	case 0x59: N2SK_NEED(5); return snprintf(t, room, "S11e encryption change v2 st %#x h %#x on %u key %u", q[0], N2SK_H(1), q[3], q[4]);
	case 0x3e:
		N2SK_NEED(1);
		if (q[0] == 0x01 || q[0] == 0x0a) { N2SK_NEED(5); return snprintf(t, room, "S11e LE conn complete (sub %#x) st %#x h %#x role %u", q[0], q[1], N2SK_H(2), q[4]); }
		if (q[0] == 0x03) { N2SK_NEED(4); return snprintf(t, room, "S11e LE conn update st %#x h %#x", q[1], N2SK_H(2)); }
		return 0;
	case 0xe4: case 0xff:
		return snprintf(t, room, "S11e VENDOR event %#x (%u bytes) %02x %02x %02x %02x", ev, ql, ql > 0 ? q[0] : 0, ql > 1 ? q[1] : 0, ql > 2 ? q[2] : 0,
		    ql > 3 ? q[3] : 0);
	default:
		return 0;
	}
#undef N2SK_NEED
#undef N2SK_H
}

/* ---------------- bt15: L2CAP signalling / AVDTP decode (how far A2DP gets) ----------------
 * dir 'i' = from the controller (the CID in the packet is ours), 'o' = to the controller (the CID is the remote's). Channels are
 * learned from Connection Request/Response on CID 1; the first PSM 0x19 channel of a link is AVDTP signalling, the next the media.
 * Returns the length of a line worth logging (0 = none); always counts bytes per channel. Never reads past the packet. */
#define N2SK_L2CH 16
struct n2sk_l2 {
	struct { uint16_t h, lcid, rcid, psm; uint8_t role, seen_i, seen_o, mpk; uint32_t rx, tx, last_tx; } ch[N2SK_L2CH];
	unsigned n;
	int kind;                                 /* bt18: last line: 0 other, 1 AVDTP (S11d), 2 config */
};

/* bt18: one Media Codec capability (service category 7) as text */
static int
n2sk_codec_text(const uint8_t *c, unsigned cl, char *t, size_t room)
{
	if (cl < 2) return snprintf(t, room, "codec ?");
	const uint8_t *q = c + 2;
	unsigned ql = cl - 2;
	if (c[1] == 0x00 && ql >= 4) {
		return snprintf(t, room, "SBC freq %#x ch-mode %#x blocks %#x subbands %#x alloc %#x bitpool %u-%u", q[0] >> 4, q[0] & 0xfu, q[1] >> 4,
		    (q[1] >> 2) & 3u, q[1] & 3u, q[2], q[3]);
	}
	if (c[1] == 0x02 && ql >= 6) {
		unsigned freq = (unsigned)(q[1] << 4) | (q[2] >> 4), br = ((unsigned)(q[3] & 0x7fu) << 16) | (unsigned)(q[4] << 8) | q[5];
		const char *obj = (q[0] & 0x80) ? "MPEG2-LC" : (q[0] & 0x40) ? "MPEG4-LC" : (q[0] & 0x20) ? "MPEG4-LTP" : (q[0] & 0x10) ? "MPEG4-scalable" : "other";
		return snprintf(t, room, "AAC obj %#x (%s) freq %#x%s ch %s vbr %u bitrate %u", q[0], obj, freq, (freq & 0x010) ? " (44.1k)" : "",
		    (q[2] & 0x0c) == 0x0c ? "1+2" : (q[2] & 0x04) ? "2" : (q[2] & 0x08) ? "1" : "?", q[3] >> 7, br);
	}
	if (c[1] == 0xff && ql >= 6) {
		unsigned vid = (unsigned)(q[0] | (q[1] << 8) | (q[2] << 16) | ((unsigned)q[3] << 24)), cid = (unsigned)(q[4] | (q[5] << 8));
		const char *nm = vid == 0x012d && cid == 0x00aa ? " (LDAC)" : vid == 0x004f ? " (aptX)" : vid == 0x00d7 ? " (aptX HD)" : "";
		return snprintf(t, room, "vendor %#x codec %#x%s (%u param bytes)", vid, cid, nm, ql - 6);
	}
	return snprintf(t, room, "codec type %#x media %#x (%u bytes)", c[1], c[0] >> 4, ql);
}

/* bt18: the capability list of an AVDTP message -> " | codec ..." pieces appended to t */
static void
n2sk_caps_text(const uint8_t *c, unsigned cn, char *t, size_t room)
{
	size_t at = strnlen(t, room);
	while (cn >= 2 && at + 4 < room) {
		unsigned cat = c[0], l = c[1];
		if (l + 2 > cn) break;
		if (cat == 7) {
			int k = snprintf(t + at, room - at, " | ");
			if (k > 0) at += (size_t)k;
			if (at < room) { k = n2sk_codec_text(c + 2, l, t + at, room - at); if (k > 0) at += (size_t)k; }
		} else if (cat == 8) {
			int k = snprintf(t + at, room - at, " | delay-report");
			if (k > 0) at += (size_t)k;
		}
		c += 2 + l;
		cn -= 2 + l;
	}
}

static const char *
n2sk_avdtp_name(unsigned id)
{
	static const char *const nm[] = { "?", "discover", "get-caps", "set-config", "get-config", "reconfig", "open", "start", "close",
		"suspend", "abort", "security", "get-all-caps", "delay-report" };
	return id < sizeof(nm) / sizeof(nm[0]) ? nm[id] : "?";
}

static int
n2sk_l2cap_text(char dir, const uint8_t *p, unsigned n, struct n2sk_l2 *st, char *t, size_t room)
{
	unsigned h, pb, l2len, cid;
	const uint8_t *d;
	unsigned dl;

	st->kind = 0;
	if (n < 9 || p[0] != 2 || room < 2) return 0;
	h = (unsigned)(p[1] | (p[2] << 8)) & 0xfffu;
	pb = (p[2] >> 4) & 3u;
	if (pb == 1) return 0;                               /* continuation fragment */
	l2len = (unsigned)(p[5] | (p[6] << 8));
	cid = (unsigned)(p[7] | (p[8] << 8));
	d = p + 9;
	dl = n - 9;
	if (dl > l2len) dl = l2len;
	if (cid == 1) {
		if (dl < 4) return snprintf(t, room, "S11l %c h%x signalling too short (%u)", dir, h, dl);
		unsigned code = d[0], id = d[1], cl = (unsigned)(d[2] | (d[3] << 8));
		const uint8_t *c = d + 4;
		unsigned cn = dl - 4;
		if (cn > cl) cn = cl;
#define N2SK_W(i) ((unsigned)(c[i] | (c[(i) + 1] << 8)))
		switch (code) {
		case 0x01: return snprintf(t, room, "S11l %c h%x command reject id %u reason %u", dir, h, id, cn >= 2 ? N2SK_W(0) : 0);
		case 0x02:
			if (cn < 4) break;
			if (st->n < N2SK_L2CH) {
				st->ch[st->n].h = (uint16_t)h; st->ch[st->n].psm = (uint16_t)N2SK_W(0);
				st->ch[st->n].lcid = dir == 'o' ? (uint16_t)N2SK_W(2) : 0;
				st->ch[st->n].rcid = dir == 'i' ? (uint16_t)N2SK_W(2) : 0;
				st->ch[st->n].role = 0; st->ch[st->n].seen_i = st->ch[st->n].seen_o = 0; st->ch[st->n].rx = st->ch[st->n].tx = 0;
				st->n++;
			}
			return snprintf(t, room, "S11l %c h%x conn req psm %#x scid %#x", dir, h, N2SK_W(0), N2SK_W(2));
		case 0x03:
			if (cn < 8) break;
			for (unsigned i = 0; i < st->n; i++) {
				if (st->ch[i].h != h) continue;
				if (dir == 'i' && st->ch[i].lcid == N2SK_W(2) && st->ch[i].rcid == 0) { st->ch[i].rcid = (uint16_t)N2SK_W(0); break; }
				if (dir == 'o' && st->ch[i].rcid == N2SK_W(2) && st->ch[i].lcid == 0) { st->ch[i].lcid = (uint16_t)N2SK_W(0); break; }
			}
			return snprintf(t, room, "S11l %c h%x conn rsp dcid %#x scid %#x result %u status %u", dir, h, N2SK_W(0), N2SK_W(2), N2SK_W(4), N2SK_W(6));
		case 0x04: if (cn < 4) break; st->kind = 2; return snprintf(t, room, "S11l %c h%x config req dcid %#x flags %u (%u option bytes)", dir, h, N2SK_W(0), N2SK_W(2), cn - 4);
		case 0x05: if (cn < 6) break; st->kind = N2SK_W(4) ? 0 : 2; return snprintf(t, room, "S11l %c h%x config rsp scid %#x result %u", dir, h, N2SK_W(0), N2SK_W(4));
		case 0x06: if (cn < 4) break; return snprintf(t, room, "S11l %c h%x disconn req dcid %#x scid %#x", dir, h, N2SK_W(0), N2SK_W(2));
		case 0x07: if (cn < 4) break; return snprintf(t, room, "S11l %c h%x disconn rsp dcid %#x scid %#x", dir, h, N2SK_W(0), N2SK_W(2));
		case 0x0a: if (cn < 2) break; return snprintf(t, room, "S11l %c h%x info req type %u", dir, h, N2SK_W(0));
		case 0x0b: if (cn < 4) break; return snprintf(t, room, "S11l %c h%x info rsp type %u result %u", dir, h, N2SK_W(0), N2SK_W(2));
		default: return snprintf(t, room, "S11l %c h%x signalling code %#x (%u bytes)", dir, h, code, cn);
		}
#undef N2SK_W
		return snprintf(t, room, "S11l %c h%x signalling code %#x too short (%u)", dir, h, code, cn);
	}
	for (unsigned i = 0; i < st->n; i++) {
		if (st->ch[i].h != h || cid != (dir == 'i' ? st->ch[i].lcid : st->ch[i].rcid) || cid == 0) continue;
		if (dir == 'i') st->ch[i].rx += n; else st->ch[i].tx += n;
		if (st->ch[i].psm == 0x19 && st->ch[i].role == 0) {
			/* AVDTP: is this the signalling or the media channel? the first 0x19 channel of the link is signalling */
			unsigned first = 1;
			for (unsigned j = 0; j < i; j++) if (st->ch[j].h == h && st->ch[j].psm == 0x19) first = 0;
			st->ch[i].role = first ? 1 : 2;
		}
		if (st->ch[i].psm == 0x19 && st->ch[i].role == 1 && dl >= 2) {
			unsigned pt = (d[0] >> 2) & 3u, mt = d[0] & 3u, sig = d[1] & 0x3fu;
			static const char *const mts[] = { "cmd", "general-reject", "accept", "reject" };
			if (pt != 0) return 0;
			st->kind = 1;
			int k = snprintf(t, room, "S11d %c h%x AVDTP %s %s (label %u, %u bytes)", dir, h, n2sk_avdtp_name(sig), mts[mt], d[0] >> 4, dl);
			if (k < 0 || (size_t)k >= room) return k;
			if (mt == 3 && dl >= 3) snprintf(t + k, room - (size_t)k, " error %#x%s", d[dl - 1], (sig == 3 || sig == 5) && dl >= 4 ? " (category in the byte before)" : "");
			else if (mt == 2 && sig == 1) {
				size_t at = (size_t)k;
				for (unsigned e = 2; e + 1 < dl && at + 24 < room; e += 2) {
					int w = snprintf(t + at, room - at, " | seid %u%s %s %s", d[e] >> 2, (d[e] & 2) ? " in-use" : "", (d[e + 1] >> 4) == 0 ? "audio" : "media?",
					    (d[e + 1] & 8) ? "SNK" : "SRC");
					if (w > 0) at += (size_t)w;
				}
			} else if (mt == 2 && (sig == 2 || sig == 12)) n2sk_caps_text(d + 2, dl - 2, t, room);
			else if (mt == 0 && sig == 3 && dl > 4) { snprintf(t + k, room - (size_t)k, " acp %u int %u", d[2] >> 2, d[3] >> 2); n2sk_caps_text(d + 4, dl - 4, t, room); }
			else if (mt == 0 && sig == 5 && dl > 3) n2sk_caps_text(d + 3, dl - 3, t, room);
			else if (mt == 0 && dl >= 3) snprintf(t + k, room - (size_t)k, " seid %u", d[2] >> 2);
			return (int)strnlen(t, room);
		}
		if (st->ch[i].psm == 0x19 && st->ch[i].role == 2 && st->ch[i].mpk < 3) {
			/* bt18: the first media packets' first 16 bytes: RTP (0x80, payload type), then SBC (frame count + 0x9c) or AAC (LATM) */
			st->ch[i].mpk++;
			st->kind = 1;
			if (dir == 'i') st->ch[i].seen_i = 1; else st->ch[i].seen_o = 1;
			char hx[3 * 16 + 1];
			unsigned m = dl < 16 ? dl : 16;
			for (unsigned b = 0; b < m; b++) snprintf(hx + 3 * b, 4, " %02x", d[b]);
			hx[3 * m] = 0;
			return snprintf(t, room, "S11d %c h%x media packet %u (%u bytes):%s", dir, h, st->ch[i].mpk, dl, hx);
		}
		if (dir == 'i' ? !st->ch[i].seen_i : !st->ch[i].seen_o) {
			if (dir == 'i') st->ch[i].seen_i = 1; else st->ch[i].seen_o = 1;
			return snprintf(t, room, "S11l %c h%x first data on psm %#x%s cid %#x (%u bytes)", dir, h, st->ch[i].psm,
			    st->ch[i].psm == 0x19 ? (st->ch[i].role == 2 ? " AVDTP-MEDIA" : " AVDTP") : "", cid, dl);
		}
		return 0;
	}
	return 0;
}

/* bt18 diagnostic (kern.n2bt_a2dp_filter, default 0 = off): in AVDTP get-caps / get-all-caps accepts (either direction) the Media Codec
 * capability of 1 = AAC, 2 = every non-SBC codec is turned into an unknown vendor codec (0xff, vendor 0xffffffff, codec 0xffff) of the
 * same length, so the other side picks SBC. Only single-fragment packets; returns the number of codecs hidden. */
static int
n2sk_avdtp_filter(const struct n2sk_l2 *st, char dir, uint8_t *p, unsigned n, unsigned mode)
{
	if (!mode || n < 11 || p[0] != 2 || ((p[2] >> 4) & 3u) == 1) return 0;
	unsigned h = (unsigned)(p[1] | (p[2] << 8)) & 0xfffu, l2len = (unsigned)(p[5] | (p[6] << 8)), cid = (unsigned)(p[7] | (p[8] << 8));
	uint8_t *d = p + 9;
	unsigned dl = n - 9, hidden = 0;
	if (dl > l2len) dl = l2len;
	int found = 0;
	for (unsigned i = 0; i < st->n; i++)
		if (st->ch[i].h == h && st->ch[i].psm == 0x19 && st->ch[i].role == 1 && cid == (dir == 'i' ? st->ch[i].lcid : st->ch[i].rcid)) found = 1;
	if (!found || dl < 2 || ((d[0] >> 2) & 3u) != 0 || (d[0] & 3u) != 2 || ((d[1] & 0x3fu) != 2 && (d[1] & 0x3fu) != 12)) return 0;
	uint8_t *c = d + 2;
	unsigned cn = dl - 2;
	while (cn >= 2) {
		unsigned cat = c[0], l = c[1];
		if (l + 2 > cn) break;
		if (cat == 7 && l >= 8 && ((mode == 1 && c[3] == 0x02) || (mode >= 2 && c[3] != 0x00))) {
			c[3] = 0xff;
			for (unsigned b = 4; b < 10; b++) c[b] = 0xff;
			hidden++;
		}
		c += 2 + l;
		cn -= 2 + l;
	}
	return (int)hidden;
}

static void
n2sk_l2_forget(struct n2sk_l2 *st, unsigned h)
{
	unsigned w = 0;
	for (unsigned i = 0; i < st->n; i++) if (st->ch[i].h != h) st->ch[w++] = st->ch[i];
	st->n = w;
}

/* ---------------- bt17: ACL credits / length / handle gate (bluetoothd -> controller) ----------------
 * [V phone bt36a]: the firmware asserted (hcit_mtk_uart.c #500) during A2DP streaming (837 ACL packets out, up to 700 bytes) and
 * again 100 ms after a restart, when bluetoothd's queued media packets for the old link reached the fresh controller [I].
 * The controller's limits come from the Command Complete of Read Buffer Size 0x1005 (ACL length / buffers; S8 saw 1021 / 8) and
 * LE Read Buffer Size 0x2002 / v2 0x2060 (0 buffers = shared with ACL). Kept in the kernel:
 *   hold   an ACL packet while the controller has no free buffer (in flight >= buffers; Number Of Completed Packets frees them),
 *   drop   (with a synthetic Number Of Completed Packets so bluetoothd's own credits stay right) ACL for a handle that is not
 *          connected, or longer than the controller's maximum. */
#define N2SK_ACLH 8
struct n2sk_acl {
	uint16_t h[N2SK_ACLH], out[N2SK_ACLH];
	uint8_t le[N2SK_ACLH];
	unsigned nh;
	uint16_t acl_max, le_max, acl_bufs, le_bufs;
	uint32_t out_acl, out_le, max_out, holds, drops, stall_resets, sent;
};

static void
n2sk_acl_init(struct n2sk_acl *a)
{
	memset(a, 0, sizeof(*a));
	a->acl_max = 1021; a->acl_bufs = 8;      /* MT6893 S8 Read Buffer Size until bluetoothd's own read is seen */
}

static int
n2sk_acl_find(const struct n2sk_acl *a, unsigned h)
{
	for (unsigned i = 0; i < a->nh; i++) if (a->h[i] == h) return (int)i;
	return -1;
}

static void
n2sk_acl_drop_handle(struct n2sk_acl *a, unsigned i)
{
	if (a->le[i] && a->le_bufs) a->out_le -= a->out[i]; else a->out_acl -= a->out[i];
	a->nh--;
	a->h[i] = a->h[a->nh]; a->out[i] = a->out[a->nh]; a->le[i] = a->le[a->nh];
}

/* controller -> bluetoothd events that change the accounting; returns 1 when a limit line is worth logging */
static int
n2sk_acl_event(struct n2sk_acl *a, const uint8_t *p, unsigned n)
{
	if (n < 4 || p[0] != 4) return 0;
	switch (p[1]) {
	case 0x0e:
		if (n >= 14 && p[4] == 0x05 && p[5] == 0x10 && p[6] == 0) {
			a->acl_max = (uint16_t)(p[7] | (p[8] << 8)); a->acl_bufs = (uint16_t)(p[10] | (p[11] << 8));
			return 1;
		}
		if (n >= 10 && (p[4] == 0x02 || p[4] == 0x60) && p[5] == 0x20 && p[6] == 0) {
			a->le_max = (uint16_t)(p[7] | (p[8] << 8)); a->le_bufs = p[9];
			return 1;
		}
		return 0;
	case 0x03:
		if (n >= 14 && p[3] == 0 && p[12] == 1 && a->nh < N2SK_ACLH) {
			unsigned h = (unsigned)(p[4] | (p[5] << 8)) & 0xfffu;
			if (n2sk_acl_find(a, h) < 0) { a->h[a->nh] = (uint16_t)h; a->out[a->nh] = 0; a->le[a->nh] = 0; a->nh++; }
		}
		return 0;
	case 0x3e:
		if (n >= 8 && (p[3] == 0x01 || p[3] == 0x0a) && p[4] == 0 && a->nh < N2SK_ACLH) {
			unsigned h = (unsigned)(p[5] | (p[6] << 8)) & 0xfffu;
			if (n2sk_acl_find(a, h) < 0) { a->h[a->nh] = (uint16_t)h; a->out[a->nh] = 0; a->le[a->nh] = 1; a->nh++; }
		}
		return 0;
	case 0x05:
		if (n >= 7 && p[3] == 0) {
			int i = n2sk_acl_find(a, (unsigned)(p[4] | (p[5] << 8)) & 0xfffu);
			if (i >= 0) n2sk_acl_drop_handle(a, (unsigned)i);
		}
		return 0;
	case 0x13: {
		unsigned nh = p[3];
		for (unsigned k = 0; k < nh && 4 + 4 * k + 3 < n; k++) {
			unsigned h = (unsigned)(p[4 + 4 * k] | (p[5 + 4 * k] << 8)) & 0xfffu, c = (unsigned)(p[6 + 4 * k] | (p[7 + 4 * k] << 8));
			int i = n2sk_acl_find(a, h);
			if (i < 0) continue;
			if (c > a->out[i]) c = a->out[i];
			a->out[i] = (uint16_t)(a->out[i] - c);
			if (a->le[i] && a->le_bufs) a->out_le -= c; else a->out_acl -= c;
		}
		return 0;
	}
	default:
		return 0;
	}
}

/* an ACL packet from bluetoothd: 0 send, 1 hold (no buffer), 2 drop (not connected), 3 drop (too long) */
static int
n2sk_acl_gate(struct n2sk_acl *a, const uint8_t *p, unsigned n)
{
	if (n < 5 || p[0] != 2) return 0;
	unsigned h = (unsigned)(p[1] | (p[2] << 8)) & 0xfffu, len = (unsigned)(p[3] | (p[4] << 8));
	int i = n2sk_acl_find(a, h);
	if (i < 0) return 2;
	bool le = a->le[i] && a->le_bufs;
	if (len > (le ? a->le_max : a->acl_max)) return 3;
	if (le ? a->out_le >= a->le_bufs : a->out_acl >= a->acl_bufs) return 1;
	return 0;
}

static void
n2sk_acl_sent(struct n2sk_acl *a, const uint8_t *p)
{
	int i = n2sk_acl_find(a, (unsigned)(p[1] | (p[2] << 8)) & 0xfffu);
	if (i < 0) return;
	a->out[i]++;
	a->sent++;
	if (a->le[i] && a->le_bufs) a->out_le++; else a->out_acl++;
	if (a->out_acl + a->out_le > a->max_out) a->max_out = a->out_acl + a->out_le;
}

/* bt18: AVDTP lines (S11d) have their own cap (100); L2CAP config lines only the first 40 (non-zero results always count as other) */
static void
n2sk_l2_log(const struct n2sk_l2 *st, const char *t, uint32_t *other, uint32_t *avdtp, uint32_t *cfg)
{
	if (st->kind == 1) { if (*avdtp < 100) { (*avdtp)++; n2bt_trail("%s", t); } return; }
	if (st->kind == 2) { if (*cfg < 40) { (*cfg)++; n2bt_trail("%s", t); } return; }
	if (*other < 200) { (*other)++; n2bt_trail("%s", t); }
}

/* a bluetoothd command worth a line: link control (OGF 1), link policy (OGF 2), LE connection/scan/advertising switches */
static bool
n2sk_cmd_noteworthy(uint16_t op)
{
	return (op >> 10) == 1 || (op >> 10) == 2 || op == 0x200a || op == 0x200c || op == 0x200d || op == 0x2039 || op == 0x2042 || op == 0x2043;
}

/* bt7: a command the kernel sends on its own (logged 'i'), and an answer the kernel keeps */
static void
n2sk_inject(const uint8_t *c, unsigned n)
{
	n2sk_dump("out", c, n);
	lck_mtx_lock(&n2sk_lock);
	n2sk_hist_add('o', c, n);
	n2sk_clog_add((uint16_t)(c[1] | (c[2] << 8)), 'i');
	lck_mtx_unlock(&n2sk_lock);
	(void)n2bt_hci_send(c, n);
}

static void
n2sk_swallowed(const uint8_t *b, unsigned n, uint16_t op, uint8_t st)
{
	n2sk_dump("in ", b, n);
	lck_mtx_lock(&n2sk_lock);
	n2sk_hist_add('i', b, n);
	n2sk_clog_status(op, st);
	lck_mtx_unlock(&n2sk_lock);
}

/* ---------------- the bridge ---------------- */
static uint8_t n2sk_pkt[SK_TXQ];
static uint8_t n2sk_in[2048];

int
n2bt_transport_run(void)
{
	uint8_t a[6], lsb[6];
	int e;
	uint32_t errs = 0, idle = 0, cmds_said = 0, vsc_said = 0, readdr_said = 0;
	uint8_t stash[64];
	unsigned stash_n = 0;
	bool readdr_pending = false;
	uint64_t readdr_deadline = 0;
	uint16_t wd_op = 0;                       /* bt6: the last forwarded command still without Command Complete/Status */
	unsigned wd_len = 0;
	uint64_t wd_since = 0;
	uint32_t wd_said = 0;
	/* bt7 [V phone bt25a]: LE Set Scan Parameters (0x200b) answered 0x0c Command Disallowed by the controller, then bluetoothd
	 * exited (every connection). Before each 0x200b the kernel sends LE Set Scan Enable(0) (answer swallowed); a 0x0c answer is
	 * retried once the same way, then turned into status 0 so bluetoothd goes on (no LE scan results in that case). */
	static const uint8_t scan_off[6] = { 1, 0x0c, 0x20, 2, 0, 0 };
	uint8_t scan_cmd[64];
	unsigned scan_n = 0;
	int scan_st = 0;                          /* 0 idle, 1 our Scan Enable(0) in flight, 2 bluetoothd's 0x200b in flight */
	bool scan_retried = false;
	uint8_t scan_off_st = 0xff;
	uint64_t scan_deadline = 0;
	uint32_t scan_said = 0;
	/* bt8 */
	bool want_restart = false;
	int links = 0;                            /* bt11: open ACL/LE links (connection / disconnection complete) */
	uint64_t last_traffic = mach_absolute_time();
	unsigned rec_n = 0;
	static uint8_t seen[32];                  /* event codes already reported */
	uint32_t ev_said = 0, cmd_said = 0, err_said = 0;
	bool acl_rx_first = true, acl_tx_first = true, was_idle = false;
	struct n2sk_rt last_rt = { 0 };
	uint64_t stats_next = 0;
	char et[200];
	static struct n2sk_l2 l2st;               /* bt15 */
	static struct n2sk_acl acl;               /* bt17 */
	uint64_t acl_hold_since = 0, acl_next = 0, restart_at = 0;
	uint32_t z_said = 0, paced = 0, rec_backoff = 0;
	bool acl_blocked = false;
	uint64_t mode_since = mach_absolute_time(), bridge_t0 = mach_absolute_time();
	uint32_t mode_tx0 = 0, mode_rx0 = 0;
	uint64_t rec_win[10] = { 0 };
	unsigned rec_k = 0;
	uint32_t l2_said = 0, d_said = 0, cfg_said = 0;

	n2bt_bdaddr(lsb);
	n2sk_acl_init(&acl);
	for (int i = 0; i < 6; i++) a[i] = lsb[5 - i];
	n2bt_trail("S10a registry service \"bluetooth\" (local-mac-address %02x:%02x:%02x:%02x:%02x:%02x, transport-encoding 3)", a[0], a[1], a[2], a[3], a[4], a[5]);
	if ((e = n2bt_registry_publish(a)) != 0) { n2bt_trail("S10a registry failed %d", e); return e; }
	n2bt_trail("S10b nodes /dev/btwake btreset btpoweroff");
	if ((e = n2sk_nodes()) != 0) { n2bt_trail("S10b nodes failed %d", e); return e; }
	n2bt_trail("S10c Skywalk kernel pipe");
	if ((e = n2sk_nexus_init()) != 0) { n2bt_trail("S10c Skywalk failed %d", e); return e; }
	n2bt_trail("S10d kernel control com.apple.uart.sk.bluetooth");
	if ((e = n2sk_ctl_register()) != 0) { n2bt_trail("S10d kernel control failed %d", e); return e; }
	n2bt_trail("S10e bridge runs; waiting for bluetoothd");
	n2bt_set_state("up");
	BT1("bridge runs (address %02x:%02x:%02x:%02x:%02x:%02x)\n", a[0], a[1], a[2], a[3], a[4], a[5]);
	for (;;) {
		int len;
		acl_blocked = false;
		for (unsigned burst = 0; !readdr_pending && scan_st == 0 && burst < 16; burst++) {
			/* what bluetoothd wrote, at most 16 packets before the next look at the controller (bt8: ACL bursts must not
			 * starve the RX side); held while the kernel has a command out */
			unsigned have, n;
			lck_mtx_lock(&n2sk_lock);
			have = n2sk_txq_count;
			if (have > 0 && (n2sk_txq[0] < 1 || n2sk_txq[0] > 5 || n2sk_txq[0] == 4)) {
				BT1("%u bytes written begin with %02x (not an H4 type): dropped\n", have, n2sk_txq[0]);
				n2sk_txq_count = 0;
				lck_mtx_unlock(&n2sk_lock);
				break;
			}
			n = n2sk_h4_len(n2sk_txq, have);
			if (n == 0 || n > have) {
				if (n > SK_TXQ) { BT1("packet of %u bytes longer than the queue: dropped\n", n); n2sk_txq_count = 0; }
				lck_mtx_unlock(&n2sk_lock);
				break;
			}
			if (n2sk_txq[0] == 2 && n2bt_tx_mode != 0) {   /* bt17: ACL gate (credits, pacing, handle, length); bt20: not in mode 0 */
				int g = n2sk_acl_gate(&acl, n2sk_txq, n);
				uint64_t now = mach_absolute_time();
				if (g == 1 || (g == 0 && n2bt_tx_mode == 3 && n2bt_tx_bps && now < acl_next)) {
					if (g == 1) {
						uint64_t held_ns = 0;
						if (!acl_hold_since) { acl_hold_since = now; acl.holds++; }
						absolutetime_to_nanoseconds(now - acl_hold_since, &held_ns);
						if (held_ns > 2000ull * NSEC_PER_MSEC) {   /* lost completions: forget the in-flight counts */
							for (unsigned q = 0; q < acl.nh; q++) acl.out[q] = 0;
							acl.out_acl = acl.out_le = 0;
							acl.stall_resets++;
							acl_hold_since = 0;
							lck_mtx_unlock(&n2sk_lock);
							n2bt_trail("S11p no Number Of Completed Packets for 2 s with %u buffers in use: in-flight counts reset (%u)", acl.acl_bufs,
							    acl.stall_resets);
							continue;
						}
					} else {
						paced++;
					}
					acl_blocked = true;
					lck_mtx_unlock(&n2sk_lock);
					break;                          /* stays queued */
				}
				acl_hold_since = 0;
				if (g == 2 || g == 3) {
					unsigned h = (unsigned)(n2sk_txq[1] | (n2sk_txq[2] << 8)) & 0xfffu, l = (unsigned)(n2sk_txq[3] | (n2sk_txq[4] << 8));
					uint8_t nocp[8] = { 4, 0x13, 5, 1, (uint8_t)h, (uint8_t)(h >> 8), 1, 0 };
					memmove(n2sk_txq, n2sk_txq + n, have - n);
					n2sk_txq_count = have - n;
					acl.drops++;
					lck_mtx_unlock(&n2sk_lock);
					if (z_said++ < 20) n2bt_trail("S11z ACL for h %#x (%u bytes) dropped: %s (limit %u); completion faked for bluetoothd", h, l,
					    g == 2 ? "handle not connected" : "longer than the controller's maximum", acl.acl_max);
					n2sk_deliver(nocp, sizeof(nocp));
					continue;
				}
			}
			memcpy(n2sk_pkt, n2sk_txq, n);
			memmove(n2sk_txq, n2sk_txq + n, have - n);
			n2sk_txq_count = have - n;
			lck_mtx_unlock(&n2sk_lock);
			n2sk_dump("out", n2sk_pkt, n);
			lck_mtx_lock(&n2sk_lock);
			n2sk_hist_add('o', n2sk_pkt, n);
			lck_mtx_unlock(&n2sk_lock);
			idle = 0;
			was_idle = false;
			n2sk_count('o', n2sk_pkt, n, &n2sk_rt);
			if (n2sk_pkt[0] == 1 && cmd_said < 60 && n2sk_cmd_noteworthy((uint16_t)(n2sk_pkt[1] | (n2sk_pkt[2] << 8)))) {
				cmd_said++;
				n2bt_trail("S11c bluetoothd command %#06x (%u bytes)", n2sk_pkt[1] | (n2sk_pkt[2] << 8), n);
			}
			if (n2sk_pkt[0] == 2) {
				int ll = n2sk_l2cap_text('o', n2sk_pkt, n, &l2st, et, sizeof(et));
				if (ll > 0) n2sk_l2_log(&l2st, et, &l2_said, &d_said, &cfg_said);
				if (n2bt_a2dp_filter) {
					int hd = n2sk_avdtp_filter(&l2st, 'o', n2sk_pkt, n, n2bt_a2dp_filter);
					if (hd > 0 && d_said < 100) { d_said++; n2bt_trail("S11d DIAGNOSTIC filter %u: %d codec(s) hidden in our capabilities", n2bt_a2dp_filter, hd); }
				}
			}
			if (n2sk_pkt[0] == 2 && n >= 5 && acl_tx_first) {
				acl_tx_first = false;
				n2bt_trail("S11a first ACL to the controller after a connection: h %#x flags %#x len %u", (n2sk_pkt[1] | (n2sk_pkt[2] << 8)) & 0xfff,
				    n2sk_pkt[2] >> 4, n);
			}
			if (n2sk_pkt[0] == 3 && n2sk_rt.sco_tx == 1) n2bt_trail("S11a first SCO packet to the controller (%u bytes)", n);
			if (n2sk_pkt[0] == 1 && cmds_said < 6) {
				cmds_said++;
				n2bt_trail("S10i bluetoothd command %#06x (%u bytes)", n2sk_pkt[1] | (n2sk_pkt[2] << 8), n);
			}
			if (n2sk_pkt[0] == 1) {
				bool here = false;
				lck_mtx_lock(&n2sk_lock);
				n2sk_clog_add((uint16_t)(n2sk_pkt[1] | (n2sk_pkt[2] << 8)), 'c');
				lck_mtx_unlock(&n2sk_lock);
				if (n2sk_pkt[1] == 0x03 && n2sk_pkt[2] == 0x0c) { links = 0; l2st.n = 0; acl.nh = 0; acl.out_acl = acl.out_le = 0; }   /* HCI_Reset drops every link */
				here = n2sk_answer_here(n2sk_pkt, n);
				if (here) {
					lck_mtx_lock(&n2sk_lock);
					if (n2sk_clog_n) n2sk_clog[n2sk_clog_n - 1].how = 'k';
					lck_mtx_unlock(&n2sk_lock);
					if (vsc_said < 12) { vsc_said++; n2bt_trail("S10j vendor command %#06x answered in the kernel", n2sk_pkt[1] | (n2sk_pkt[2] << 8)); }
					continue;
				}
				if ((n2bt_quirks() & N2BT_Q_SCANFIX) && n2sk_pkt[1] == 0x0b && n2sk_pkt[2] == 0x20 && n <= sizeof(scan_cmd)) {
					memcpy(scan_cmd, n2sk_pkt, n);
					scan_n = n;
					scan_retried = false;
					n2sk_inject(scan_off, sizeof(scan_off));
					scan_st = 1;
					clock_interval_to_deadline(500, NSEC_PER_MSEC, &scan_deadline);
					continue;                       /* the loop ends: nothing else goes out until the answers are in */
				}
			}
			if (n2sk_answer_here(n2sk_pkt, n)) {
				if (vsc_said < 12) { vsc_said++; n2bt_trail("S10j vendor command %#06x answered in the kernel", n2sk_pkt[1] | (n2sk_pkt[2] << 8)); }
				continue;
			}
			n2bt_now("tx", (uint32_t)n2sk_pkt[0] << 16 | (uint32_t)(n2sk_pkt[1] | (n2sk_pkt[2] << 8)), n);
			if (n2sk_pkt[0] == 2) {
				n2sk_acl_sent(&acl, n2sk_pkt);
				if (n2bt_tx_bps) {
					uint64_t gap;
					nanoseconds_to_absolutetime((uint64_t)n * 8ull * 1000000000ull / n2bt_tx_bps, &gap);
					acl_next = mach_absolute_time() + gap;
				}
			}
			if ((e = n2bt_hci_send(n2sk_pkt, n)) != 0 && errs++ < 8) {
				BT1("send of %u bytes failed (%d)\n", n, e);
				if (err_said++ < 16) {
					uint32_t tf[4];
					n2bt_tx_fail(tf);
					n2bt_trail("S11x send of %u bytes (type %u) failed (%d): %u/%u written, LSR %#x, %u us%s", n, n2sk_pkt[0], e, tf[0], tf[1], tf[2], tf[3],
					    tf[0] ? " -> controller restart (half a packet in the firmware)" : "");
				}
			}
			if (n2sk_pkt[0] == 1) { wd_op = (uint16_t)(n2sk_pkt[1] | (n2sk_pkt[2] << 8)); wd_len = n; wd_since = mach_absolute_time(); }
		}
		len = n2bt_hci_receive(n2sk_in, sizeof(n2sk_in) - 80, 0);   /* non-blocking: what is whole now */
		if (len > 0) {
			unsigned m;
			n2bt_now("rx", (uint32_t)n2sk_in[0] << 16 | (uint32_t)(n2sk_in[1] | (n2sk_in[2] << 8)), (uint32_t)len);
			n2sk_count('i', n2sk_in, (unsigned)len, &n2sk_rt);
			if (n2sk_in[0] == 4 && len >= 2 && n2sk_acl_event(&acl, n2sk_in, (unsigned)len))
				n2bt_trail("S10b controller buffers: ACL %u bytes x %u, LE %u bytes x %u (0 = shared)", acl.acl_max, acl.acl_bufs, acl.le_max, acl.le_bufs);
			if (n2sk_in[0] == 4 && len >= 2) {
				uint8_t code = n2sk_in[1];
				int tl = n2sk_event_text(n2sk_in, (unsigned)len, et, sizeof(et));
				if (code == 0x03 || code == 0x2c || (code == 0x3e && len >= 4 && (n2sk_in[3] == 0x01 || n2sk_in[3] == 0x0a))) { acl_rx_first = true; acl_tx_first = true; }
				if ((code == 0x03 && len >= 4 && n2sk_in[3] == 0) || (code == 0x3e && len >= 5 && (n2sk_in[3] == 0x01 || n2sk_in[3] == 0x0a) && n2sk_in[4] == 0)) links++;
				if (code == 0x05 && len >= 4 && n2sk_in[3] == 0 && links > 0) links--;
				if (code == 0x05 && len >= 6 && n2sk_in[3] == 0) n2sk_l2_forget(&l2st, (unsigned)(n2sk_in[4] | (n2sk_in[5] << 8)) & 0xfffu);
				if (tl > 0 && ev_said < 200) { ev_said++; n2bt_trail("%s", et); }
				if ((code == 0x03 || code == 0x05) && ev_said < 200) n2bt_snap(code == 0x03 ? "after conn complete" : "after disconn complete");   /* bt9 */
				else if (!(seen[code >> 3] & (1u << (code & 7))) && ev_said < 200) { ev_said++; n2bt_trail("S11e first event %#04x (%d bytes)", code, len); }
				seen[code >> 3] |= (uint8_t)(1u << (code & 7));
			} else if (n2sk_in[0] == 2 && len >= 9) {
				int ll = n2sk_l2cap_text('i', n2sk_in, (unsigned)len, &l2st, et, sizeof(et));
				if (ll > 0) n2sk_l2_log(&l2st, et, &l2_said, &d_said, &cfg_said);
				if (n2bt_a2dp_filter) {
					int hd = n2sk_avdtp_filter(&l2st, 'i', n2sk_in, (unsigned)len, n2bt_a2dp_filter);
					if (hd > 0 && d_said < 100) { d_said++; n2bt_trail("S11d DIAGNOSTIC filter %u: %d codec(s) hidden in the remote's capabilities", n2bt_a2dp_filter, hd); }
				}
				if (acl_rx_first) {
					acl_rx_first = false;
					n2bt_trail("S11a first ACL from the controller after a connection: h %#x flags %#x len %d", (n2sk_in[1] | (n2sk_in[2] << 8)) & 0xfff,
					    n2sk_in[2] >> 4, len);
				}
			} else if (n2sk_in[0] == 2 && len >= 5 && acl_rx_first) {
				acl_rx_first = false;
				n2bt_trail("S11a first ACL from the controller after a connection: h %#x flags %#x len %d", (n2sk_in[1] | (n2sk_in[2] << 8)) & 0xfff,
				    n2sk_in[2] >> 4, len);
			} else if (n2sk_in[0] == 3 && n2sk_rt.sco_rx == 1) {
				n2bt_trail("S11a first SCO packet from the controller (%d bytes)", len);
			}
			m = n2sk_rewrite(n2sk_in, (unsigned)len);
			idle = 0;
			was_idle = false;
			last_traffic = mach_absolute_time();
			if (wd_op && (n2sk_is_cc(n2sk_in, (unsigned)len, wd_op) || n2sk_is_cs(n2sk_in, (unsigned)len, wd_op))) wd_op = 0;
			if (m && readdr_pending && (n2sk_is_cc(n2sk_in, m, 0xfc1a) || n2sk_is_cs(n2sk_in, m, 0xfc1a))) {
				if (readdr_said < 6) { readdr_said++; n2bt_trail("S10k address set again after HCI_Reset: status %#x", n2sk_in[1] == 0x0e ? n2sk_in[6] : n2sk_in[3]); }
				readdr_pending = false;
				lck_mtx_lock(&n2sk_lock);
				n2sk_clog_status(0xfc1a, n2sk_in[1] == 0x0e ? n2sk_in[6] : n2sk_in[3]);
				lck_mtx_unlock(&n2sk_lock);
				n2sk_deliver(stash, stash_n);              /* bluetoothd now gets its Reset answer */
				m = 0;
			}
			if (m && !readdr_pending && (n2bt_quirks() & N2BT_Q_READDR) && n2sk_is_cc(n2sk_in, m, 0x0c03) && m <= sizeof(stash)) {
				uint8_t c[10];
				if (n2bt_addr_cmd(c) == 10) {
					memcpy(stash, n2sk_in, m);
					stash_n = m;
					n2sk_dump("out", c, 10);
					lck_mtx_lock(&n2sk_lock);
					n2sk_clog_add(0xfc1a, 'i');
					lck_mtx_unlock(&n2sk_lock);
					if (n2bt_hci_send(c, 10) == 0) {
						readdr_pending = true;
						clock_interval_to_deadline(1000, NSEC_PER_MSEC, &readdr_deadline);
						m = 0;
					}
				}
			}
			if (m && scan_st == 1 && (n2sk_is_cc(n2sk_in, m, 0x200c) || n2sk_is_cs(n2sk_in, m, 0x200c))) {
				scan_off_st = n2sk_in[1] == 0x0e ? n2sk_in[6] : n2sk_in[3];
				n2sk_swallowed(n2sk_in, m, 0x200c, scan_off_st);
				n2sk_dump("out", scan_cmd, scan_n);
				if ((e = n2bt_hci_send(scan_cmd, scan_n)) != 0 && errs++ < 8) BT1("send of %u bytes failed (%d)\n", scan_n, e);
				wd_op = 0x200b; wd_len = scan_n; wd_since = mach_absolute_time();
				scan_st = 2;
				clock_interval_to_deadline(1000, NSEC_PER_MSEC, &scan_deadline);
				m = 0;
			}
			if (m && scan_st == 2 && (n2sk_is_cc(n2sk_in, m, 0x200b) || n2sk_is_cs(n2sk_in, m, 0x200b))) {
				uint8_t st = n2sk_in[1] == 0x0e ? n2sk_in[6] : n2sk_in[3];
				if (st == 0x0c && !scan_retried) {
					if (scan_said < 16) { scan_said++; n2bt_trail("S10m LE Set Scan Parameters: 0x0c after Scan Enable(0) (status %#x): retried once", scan_off_st); }
					n2sk_swallowed(n2sk_in, m, 0x200b, st);
					lck_mtx_lock(&n2sk_lock);
					n2sk_clog_add(0x200b, 'c');           /* the retry gets its own line */
					lck_mtx_unlock(&n2sk_lock);
					scan_retried = true;
					n2sk_inject(scan_off, sizeof(scan_off));
					scan_st = 1;
					clock_interval_to_deadline(500, NSEC_PER_MSEC, &scan_deadline);
					m = 0;
				} else {
					if (st == 0x0c && n2sk_in[1] == 0x0e) {
						if (scan_said < 16) { scan_said++; n2bt_trail("S10m LE Set Scan Parameters still 0x0c after the retry: status 0 given to bluetoothd (the controller keeps its own scan parameters)"); }
						n2sk_in[6] = 0;
					} else if (scan_said < 16) {
						scan_said++;
						n2bt_trail("S10m LE Set Scan Parameters status %#x after Scan Enable(0) (status %#x)%s", st, scan_off_st, scan_retried ? ", on the retry" : "");
					}
					scan_st = 0;
				}
			}
			if (m && scan_st == 0 && (n2bt_quirks() & N2BT_Q_SCANFIX) && n2sk_is_cc(n2sk_in, m, 0x200c) && n2sk_in[6] == 0x0c) {
				if (scan_said < 16) { scan_said++; n2bt_trail("S10m LE Set Scan Enable answered 0x0c by the controller: status 0 given to bluetoothd (no LE scan)"); }
				n2sk_in[6] = 0;
			}
			if (m) n2sk_deliver(n2sk_in, m);
		} else {
			if (len < 0 && errs++ < 8) {
				BT1("receive failed (%d)\n", len);
				if (err_said++ < 16) n2bt_trail("S11x receive from the controller failed (%d)", len);
			}
			/* nothing from the controller: block (no busy wait); BTIF flow control holds the controller meanwhile.
			 * 1 ms while traffic is recent (A2DP, scans), 4 ms after ~0.5 s of silence */
			lck_mtx_lock(&n2sk_lock);
			bool tx_pending = n2sk_txq_count > 0 && !acl_blocked;   /* bt17: a held ACL packet is not work */
			lck_mtx_unlock(&n2sk_lock);
			if (!tx_pending) {
				if (!was_idle) { n2bt_now("idle", 0, 0); was_idle = true; }
				/* bt11: the controller can only push 8 bytes before we read (AP RX FIFO); with a link up or traffic in the
				 * last 3 s poll every kern.n2bt_poll_us (100 us, a timer wait on arm64, not a spin), else 1 ms / 4 ms */
				uint64_t ns_quiet = 0;
				absolutetime_to_nanoseconds(mach_absolute_time() - last_traffic, &ns_quiet);
				static const uint32_t mode_poll[6] = { 100, 100, 50, 100, 20, 20 };   /* bt20 */
				uint32_t pu = n2bt_poll_us ? (n2bt_tx_mode < 6 ? mode_poll[n2bt_tx_mode] : n2bt_poll_us) : 0;
				/* bt21: adaptive: the mode's period while traffic is < 500 ms old, kern.n2bt_poll_idle_us with a quiet link */
				if (pu && ns_quiet >= 500ull * NSEC_PER_MSEC && links > 0 && n2bt_poll_idle_us > pu) pu = n2bt_poll_idle_us;
				if (pu && (links > 0 || ns_quiet < 3000ull * NSEC_PER_MSEC)) IODelay(pu);
				else IOSleep(idle < 400 ? 1 : 4);
				if (idle < 100000) idle++;
			}
		}
		if (wd_op) {
			uint64_t ns = 0;
			absolutetime_to_nanoseconds(mach_absolute_time() - wd_since, &ns);
			if (ns > 3000ull * NSEC_PER_MSEC) {
				if (wd_said++ < 12) {
					uint32_t have = 0, lsr = 0;
					uint8_t f[4];
					n2bt_rx_debug(&have, &lsr, f);
					n2bt_trail("S10w no answer in 3 s to %#06x (%u bytes); BTIF LSR %#x, rx bytes waiting %u (%02x %02x %02x %02x)", wd_op, wd_len,
					    lsr, have, f[0], f[1], f[2], f[3]);
					n2bt_diag("no answer");
				}
				wd_op = 0;
				want_restart = true;
			}
		}
		if (n2bt_reset_requested()) {           /* bt10: the firmware said it asserted (sw irq bit 25/26): no 3 s wait */
			n2bt_diag("firmware reset request");
			want_restart = true;
		}
		if (want_restart && !restart_at && n2bt_recover_on()) {
			/* bt17 recovery policy: back-off 1, 2, 4, 8, 16, 30 s between consecutive restarts (reset after 5 quiet minutes),
			 * at most 10 in any 10 minutes, and never a permanent give-up: when the window is full, the next restart waits for
			 * the oldest one to age out. bt36a stayed dead after "3 restarts in 5 minutes". */
			uint64_t now = mach_absolute_time(), d = 0, quiet = 0;
			static const uint32_t backoff_s[6] = { 1, 2, 4, 8, 16, 30 };
			uint64_t last = rec_win[(rec_k + 9) % 10], oldest = rec_win[rec_k % 10];
			if (last) absolutetime_to_nanoseconds(now - last, &quiet);
			if (!last || quiet > 300ull * 1000000000ull) rec_backoff = 0;
			nanoseconds_to_absolutetime((uint64_t)backoff_s[rec_backoff < 6 ? rec_backoff : 5] * 1000000000ull, &d);
			restart_at = now + d;
			if (oldest) {
				uint64_t age = 0, win;
				absolutetime_to_nanoseconds(now - oldest, &age);
				if (age < 600ull * 1000000000ull) {
					nanoseconds_to_absolutetime(600ull * 1000000000ull - age, &win);
					if (now + win > restart_at) restart_at = now + win;
				}
			}
			uint64_t wait_ns = 0;
			absolutetime_to_nanoseconds(restart_at - now, &wait_ns);
			n2bt_trail("S10r restart scheduled in %llu ms (back-off step %u, %u restarts so far)", wait_ns / 1000000ull, rec_backoff, rec_n);
		}
		want_restart = false;
		if (restart_at && mach_absolute_time() >= restart_at) {
			static const uint8_t hwerr[4] = { 4, 0x10, 1, 0x7e };
			restart_at = 0;
			rec_win[rec_k % 10] = mach_absolute_time();
			rec_k++;
			rec_n++;
			if (rec_backoff < 5) rec_backoff++;
			n2bt_now("restart", rec_n, 0);
			{
				uint64_t lived = 0;
				absolutetime_to_nanoseconds(mach_absolute_time() - mode_since, &lived);
				(void)n2bt_mode_next((uint32_t)(lived / 1000000ull), n2sk_rt.acl_tx - mode_tx0, n2sk_rt.acl_rx - mode_rx0);
			}
			lck_mtx_lock(&n2sk_lock);
			unsigned flushed = n2sk_txq_count;
			n2sk_txq_count = 0;                      /* bt17: nothing queued for the old controller reaches the new one */
			lck_mtx_unlock(&n2sk_lock);
			int rs = n2bt_restart("no answer / firmware reset request");
			readdr_pending = false;
			scan_st = 0;
			links = 0;
			l2st.n = 0;
			acl.nh = 0; acl.out_acl = acl.out_le = 0;
			acl_rx_first = acl_tx_first = true;
			wd_op = 0;
			mode_since = mach_absolute_time(); mode_tx0 = n2sk_rt.acl_tx; mode_rx0 = n2sk_rt.acl_rx;
			if (rs == 0) {
				n2bt_trail("S10r Hardware Error event (code 0x7e) handed to bluetoothd (restart %u, %u queued bytes dropped)", rec_n, flushed);
				n2sk_deliver(hwerr, sizeof(hwerr));
			} else {
				want_restart = true;                 /* failed: try again after the next back-off */
			}
		}
		if (scan_st == 1 && mach_absolute_time() > scan_deadline) {
			if (scan_said < 16) { scan_said++; n2bt_trail("S10m no answer to Scan Enable(0) in 500 ms: LE Set Scan Parameters sent anyway"); }
			n2sk_dump("out", scan_cmd, scan_n);
			(void)n2bt_hci_send(scan_cmd, scan_n);
			wd_op = 0x200b; wd_len = scan_n; wd_since = mach_absolute_time();
			scan_st = 2;
			clock_interval_to_deadline(1000, NSEC_PER_MSEC, &scan_deadline);
		} else if (scan_st == 2 && mach_absolute_time() > scan_deadline) {
			scan_st = 0;                            /* the 3 s watchdog reports it */
		}
		if (readdr_pending && mach_absolute_time() > readdr_deadline) {
			n2bt_trail("S10k no answer to the address command in 1 s: Reset answer handed on anyway");
			readdr_pending = false;
			n2sk_deliver(stash, stash_n);
		}
		n2bt_housekeeping();
		if (mach_absolute_time() > stats_next) {
			uint64_t up_ns = 0;
			absolutetime_to_nanoseconds(mach_absolute_time() - bridge_t0, &up_ns);
			uint32_t period_ms = up_ns < 300ull * 1000000000ull ? 15000u : 60000u;   /* bt21: quieter in the long run */
			clock_interval_to_deadline(period_ms, NSEC_PER_MSEC, &stats_next);
			if (n2sk_rt.acl_rx != last_rt.acl_rx || n2sk_rt.acl_tx != last_rt.acl_tx || n2sk_rt.sco_rx != last_rt.sco_rx || n2sk_rt.sco_tx != last_rt.sco_tx) {
				uint32_t cn[6];
				n2bt_counters(cn);
				n2bt_trail("S11s acl r%u/%uB<%u t%u/%uB<%u sco %u/%u ev %u nocp %u", n2sk_rt.acl_rx, n2sk_rt.acl_rx_b, n2sk_rt.acl_rx_max,
				    n2sk_rt.acl_tx, n2sk_rt.acl_tx_b, n2sk_rt.acl_tx_max, n2sk_rt.sco_rx, n2sk_rt.sco_tx, n2sk_rt.ev, n2sk_rt.nocp);
				n2bt_trail("S11s rsync %u ovr %u intx %u txto %u wait %u drop %u/%u rx@tx %u links %d", cn[0], cn[1], cn[4], cn[2], cn[3],
				    n2sk_txq_dropped, n2sk_rx_dropped, cn[5], links);
				{
					uint64_t ml = 0;
					absolutetime_to_nanoseconds(mach_absolute_time() - mode_since, &ml);
					n2bt_trail("S11p mode %u up %llu s: acl sent %u in-flight %u/%u max %u holds %u paced %u stalls %u drops %u", n2bt_tx_mode,
					    ml / 1000000000ull, acl.sent, acl.out_acl, acl.acl_bufs, acl.max_out, acl.holds, paced, acl.stall_resets, acl.drops);
				}
				for (unsigned i = 0; i < l2st.n; i++) {
					if (l2st.ch[i].rx + l2st.ch[i].tx < 64) continue;
					n2bt_trail("S11s h%x psm %#x%s lcid %#x rcid %#x rx %u tx %u (%u B/s)", l2st.ch[i].h, l2st.ch[i].psm,
					    l2st.ch[i].role == 2 ? " MEDIA" : l2st.ch[i].role == 1 ? " SIG" : "", l2st.ch[i].lcid, l2st.ch[i].rcid,
					    l2st.ch[i].rx, l2st.ch[i].tx, (l2st.ch[i].tx - l2st.ch[i].last_tx) / (period_ms / 1000u));
					l2st.ch[i].last_tx = l2st.ch[i].tx;
				}
				last_rt = n2sk_rt;
			}
		}
		{
			static uint32_t last_drop;
			uint32_t d;
			lck_mtx_lock(&n2sk_lock);
			d = n2sk_txq_dropped + n2sk_rx_dropped;
			lck_mtx_unlock(&n2sk_lock);
			if (d != last_drop) { BT1("dropped so far: tx bytes %u rx packets %u\n", n2sk_txq_dropped, n2sk_rx_dropped); last_drop = d; }
		}
	}
}
#endif /* NORD2 */
