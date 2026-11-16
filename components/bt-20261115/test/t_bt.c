/* host test of the pure parts of pieces 0720/0721, extracted verbatim from the kernel sources (extract.py) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#define __printflike(a, b)
#include "../src/bsd/dev/nord2_bt.h"
#define printf(...) ((void)0)
#define BT0(...) ((void)0)
#define BT1(...) ((void)0)
#define __unused
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)
/* ---- stubs for the controller side ---- */
static uint32_t quirks = N2BT_Q_DEFAULT;
uint32_t n2bt_quirks(void) { return quirks; }
uint32_t n2bt_trace_left(void) { return 0; }
static unsigned trails; void n2bt_trail(const char *fmt, ...) { (void)fmt; trails++; }
static uint8_t delivered[4096]; static unsigned delivered_n, delivered_count;
static void n2sk_deliver(const uint8_t *b, unsigned n) { memcpy(delivered, b, n); delivered_n = n; delivered_count++; }
/* BTIF FIFO model for the reassembly */
static uint8_t fifo[1 << 16]; static unsigned fifo_head, fifo_tail, fifo_burst;
#define BTIF_LSR 1u
#define BTIF_RBR_THR 2u
static uint32_t rd(uint32_t reg) {
	if (reg == BTIF_LSR) return (fifo_head < fifo_tail && fifo_burst > 0) ? 1u : 0u;
	if (fifo_burst) fifo_burst--;
	return fifo[fifo_head++];
}
static struct { uint32_t rx_pkts, rx_bytes, rx_resync, rx_overrun; } n2bt_st;
typedef uint64_t u64;
#define NSEC_PER_MSEC 1000000ull
static u64 fake_now;
static void clock_interval_to_deadline(uint32_t v, uint64_t s, u64 *d) { *d = fake_now + v * s / 1000; }
static u64 mach_absolute_time(void) { return fake_now += 1000; }
static void IODelay(unsigned us) { (void)us; fifo_burst = 3 + (unsigned)(rand() % 20); }
struct n2bt_fw { const char *name; uint32_t emi_start, emi_size; uint8_t *buf; uint32_t len; };
#include "extracted.inc"
#include "bt31a_vsc.inc"

static uint8_t *readf(const char *p, uint32_t *n) { FILE *f = fopen(p, "rb"); if (!f) return NULL; fseek(f, 0, SEEK_END); *n = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); uint8_t *b = malloc(*n); if (fread(b, 1, *n, f) != *n) { free(b); b = NULL; } fclose(f); return b; }

int main(int argc, char **argv)
{
	/* H4 lengths */
	uint8_t ev[] = { 4, 0x0e, 4, 1, 3, 0x0c, 0 }, acl[] = { 2, 1, 0x20, 3, 0, 9, 9, 9 }, cmd[] = { 1, 3, 0x0c, 0 };
	CHECK(n2bt_h4_len(ev, 7) == 7 && n2sk_h4_len(ev, 7) == 7, "event len");
	CHECK(n2bt_h4_len(acl, 8) == 8 && n2bt_h4_len(acl, 4) == 0, "acl len");
	CHECK(n2sk_h4_len(cmd, 4) == 4 && n2bt_h4_len((uint8_t[]){9}, 1) == 0, "cmd/garbage");
	/* firmware headers: the user's real files + mutations */
	if (argc > 2) {
		struct n2bt_fw fw[2] = { { "mcu", 0, 0x100000, NULL, 0 }, { "bt", 0x100000, 0x129400, NULL, 0 } };
		uint32_t off;
		for (int i = 0; i < 2; i++) {
			fw[i].buf = readf(argv[1 + i], &fw[i].len);
			CHECK(fw[i].buf != NULL, "read %s", argv[1 + i]);
			if (!fw[i].buf) continue;
			CHECK(n2bt_fw_check(&fw[i], &off) == 0, "%s accepted", fw[i].name);
			CHECK(off == (i ? 0x100000u : 0u), "%s offset %#x", fw[i].name, off);
			struct n2bt_fw swapped = fw[i]; swapped.emi_start = i ? 0 : 0x100000; swapped.emi_size = i ? 0x100000 : 0x129400;
			CHECK(n2bt_fw_check(&swapped, &off) == ERANGE, "%s refused in the other region", fw[i].name);
			uint8_t save = fw[i].buf[16]; fw[i].buf[16] = 'X';
			CHECK(n2bt_fw_check(&fw[i], &off) == EINVAL, "%s bad magic refused", fw[i].name); fw[i].buf[16] = save;
			save = fw[i].buf[20]; fw[i].buf[20] = 0x10;
			CHECK(n2bt_fw_check(&fw[i], &off) == EINVAL, "%s bad hw refused", fw[i].name); fw[i].buf[20] = save;
			struct n2bt_fw trunc = fw[i]; trunc.len -= 2;
			CHECK(n2bt_fw_check(&trunc, &off) != 0, "%s odd length refused", fw[i].name);
			free(fw[i].buf);
		}
	}
	/* answer rewrites */
	{
		uint8_t vsc[] = { 1, 0x48, 0xfd, 0 };
		delivered_count = 0;
		CHECK(n2sk_answer_here(vsc, 4) && delivered_n == 8 && delivered[1] == 0x0e && delivered[2] == 5 && delivered[4] == 0x48 && delivered[5] == 0xfd && delivered[6] == 0, "fd48 one data byte");
		uint8_t ant[] = { 1, 0xe2, 0xfc, 5, 0x15, 0, 0, 0, 1 };
		CHECK(n2sk_answer_here(ant, 9) && delivered_n == 7 + 16 && delivered[2] == 20, "fce2/15 16 bytes");
		uint8_t cal[] = { 1, 0x1b, 0xfd, 1, 0x51 };
		CHECK(n2sk_answer_here(cal, 5) && delivered_n == 7 + 64 && delivered[7] == 0x51, "fd1b 64 bytes 0x51");
		uint8_t e9[] = { 1, 0xe9, 0xfc, 0 };
		CHECK(n2sk_answer_here(e9, 4) && delivered_n == 7, "fce9 no data (bt13: as bt31a)");
		uint8_t prof[] = { 1, 0xe2, 0xfc, 5, 0x0d, 0, 0, 0, 1 };
		CHECK(n2sk_answer_here(prof, 9) && delivered_n >= 7 + 8 && delivered[6] == 0, "fce2 sub 0x0d (profiling stats, 4 x u16) gets >= 8 data bytes");
		uint8_t std[] = { 1, 0x03, 0x0c, 0 };
		CHECK(!n2sk_answer_here(std, 4), "standard command goes to the controller");
		uint8_t wbd[] = { 1, 0x01, 0xfc, 6, 1, 2, 3, 4, 5, 6 };
		quirks = N2BT_Q_BCM_BDADDR;
		CHECK(n2sk_answer_here(wbd, 10) && delivered[4] == 0x01 && delivered[5] == 0xfc, "fc01 answered with only the bdaddr quirk");
		CHECK(!n2sk_answer_here(e9, 4), "VSC forwarded when VSC quirk off");
		quirks = N2BT_Q_DEFAULT;
		uint8_t wmt[] = { 4, 0xe4, 5, 2, 6, 1, 0, 0 };
		CHECK(n2sk_rewrite(wmt, 8) == 0, "WMT event dropped");
		uint8_t ed[] = { 4, 0x0f, 4, 1, 1, 0x5b, 0x0c };
		CHECK(n2sk_rewrite(ed, 7) == 7 && ed[1] == 0x0e && ed[6] == 0 && ed[4] == 0x5b, "0c5b unknown -> done");
		uint8_t sc[7 + 64] = { 4, 0x0e, 0x44, 1, 0x02, 0x10, 0 };
		CHECK(n2sk_rewrite(sc, sizeof(sc)) == sizeof(sc) && sc[7 + 25] == 0xf7 && sc[7 + 28] == 0x7f && sc[7 + 24] == 0x60, "LE command bits");
		/* bt5: Erroneous Data Reporting (the bt22a blocker) */
		uint8_t sc2[7 + 64] = { 4, 0x0e, 0x44, 1, 0x02, 0x10, 0 };
		CHECK(n2sk_rewrite(sc2, sizeof(sc2)) == sizeof(sc2) && (sc2[7 + 18] & 0x0c) == 0x0c, "octet 18 bits 2,3 advertised");
		uint8_t wed[] = { 1, 0x5b, 0x0c, 1, 0 }, red[] = { 1, 0x5a, 0x0c, 0 };
		delivered_count = 0;
		CHECK(n2sk_answer_here(wed, 5) && delivered_n == 7 && delivered[1] == 0x0e && delivered[4] == 0x5b && delivered[5] == 0x0c && delivered[6] == 0, "0x0c5b answered status 0");
		CHECK(n2sk_answer_here(red, 4) && delivered_n == 8 && delivered[2] == 5 && delivered[4] == 0x5a && delivered[6] == 0 && delivered[7] == 0, "0x0c5a answered status 0 mode 0");
		/* the real bt22a init sequence must reach the controller unchanged except 0x0c5b */
		static const uint16_t init[] = { 0x0c03, 0x1001, 0x1002, 0x1009, 0x1003, 0x1004, 0x2003, 0x0c01, 0x0c63, 0x2001, 0x0c56, 0x0c6d, 0x2060,
			0x202f, 0x2031, 0x2024, 0x201c, 0x200f, 0x1005, 0x2074, 0x0c33, 0x0c31 };   /* bt8: 0x204a answered here (below) */
		unsigned fwd = 0;
		for (unsigned i = 0; i < sizeof(init) / sizeof(init[0]); i++) { uint8_t c[4] = { 1, (uint8_t)init[i], (uint8_t)(init[i] >> 8), 0 }; fwd += !n2sk_answer_here(c, 4); }
		CHECK(fwd == sizeof(init) / sizeof(init[0]), "all %u bt22a init commands still go to the controller (%u)", (unsigned)(sizeof(init) / sizeof(init[0])), fwd);
		/* bt6: the two long commands after which the controller went silent (bt23a) */
		static uint8_t wln[4 + 248] = { 1, 0x13, 0x0c, 248, 'i', 'P', 'h', 'o', 'n', 'e' }, weir[4 + 241] = { 1, 0x52, 0x0c, 241, 0 };
		unsigned t0 = trails;
		CHECK(n2sk_answer_here(wln, sizeof(wln)) && delivered_n == 7 && delivered[1] == 0x0e && delivered[2] == 4 && delivered[4] == 0x13 && delivered[5] == 0x0c && delivered[6] == 0, "0x0c13 Write Local Name answered status 0");
		CHECK(n2sk_answer_here(weir, sizeof(weir)) && delivered_n == 7 && delivered[4] == 0x52 && delivered[5] == 0x0c && delivered[6] == 0, "0x0c52 Write EIR answered status 0");
		CHECK(trails == t0 + 2, "S10l breadcrumb per locally answered command (%u)", trails - t0);
		/* bt8: 0x204a answered here (legacy LE mode); LE scan commands forwarded unless quirk 0x200 */
		{
			uint8_t pal[] = { 1, 0x4a, 0x20, 0 }, ssp[] = { 1, 0x0b, 0x20, 7, 0, 0xe0, 1, 0x30, 0, 0, 0 }, sse[] = { 1, 0x0c, 0x20, 2, 1, 0 };
			CHECK(n2sk_answer_here(pal, 4) && delivered_n == 8 && delivered[4] == 0x4a && delivered[5] == 0x20 && delivered[6] == 0 && delivered[7] == 0x10, "0x204a answered: status 0 size 0x10");
			CHECK(!n2sk_answer_here(ssp, sizeof(ssp)) && !n2sk_answer_here(sse, sizeof(sse)), "LE scan commands go to the controller by default");
			quirks = N2BT_Q_DEFAULT | N2BT_Q_LESCAN_LOCAL;
			CHECK(n2sk_answer_here(ssp, sizeof(ssp)) && delivered[4] == 0x0b && delivered[6] == 0 && n2sk_answer_here(sse, sizeof(sse)) && delivered[4] == 0x0c, "LE scan answered locally with quirk 0x200");
			quirks = N2BT_Q_DEFAULT & ~N2BT_Q_NOEXT;
			CHECK(!n2sk_answer_here(pal, 4), "0x204a forwarded without quirk 0x100");
			quirks = N2BT_Q_DEFAULT;
		}
		/* bt7: command list since HCI_Reset + status parsing */
		{
			uint16_t op = 0; uint8_t st = 0;
			uint8_t cc[] = { 4, 0x0e, 4, 1, 0x0b, 0x20, 0x0c }, cs[] = { 4, 0x0f, 4, 0x0c, 1, 0x05, 0x04 }, ev[] = { 4, 0x3e, 3, 2, 1, 0 }, nop[] = { 4, 0x0e, 4, 1, 0, 0, 0 };
			CHECK(n2sk_status_of(cc, 7, &op, &st) && op == 0x200b && st == 0x0c, "CC status parsed");
			CHECK(n2sk_status_of(cs, 7, &op, &st) && op == 0x0405 && st == 0x0c, "CS status parsed");
			CHECK(!n2sk_status_of(ev, 6, &op, &st) && !n2sk_status_of(nop, 7, &op, &st), "LE meta / NOP event not a command answer");
			n2sk_clog_add(0x0c03, 'c'); n2sk_clog_add(0x200c, 'i'); n2sk_clog_add(0x200b, 'c'); n2sk_clog_add(0x200b, 'c');
			n2sk_clog_status(0x200b, 0x0c); n2sk_clog_status(0x200c, 0);
			CHECK(n2sk_clog_n == 4 && n2sk_clog[2].st == 0x0c && n2sk_clog[3].st == 0xff && n2sk_clog[1].st == 0 && n2sk_clog[0].st == 0xff, "status goes to the oldest unanswered entry");
			n2sk_clog_add(0xfc1a, 'i'); n2sk_clog_add(0x0c03, 'i');
			CHECK(n2sk_clog_n == 6, "injected Reset does not clear the list");
			n2sk_clog_add(0x0c03, 'c');
			CHECK(n2sk_clog_n == 1 && n2sk_clog[0].op == 0x0c03, "bluetoothd's HCI_Reset starts a new list");
			for (unsigned i = 0; i < 300; i++) n2sk_clog_add(0x1001, 'c');
			CHECK(n2sk_clog_n == N2SK_CLOG && n2sk_clog_over == 300 + 1 - N2SK_CLOG, "list bounded (%u over)", n2sk_clog_over);
		}
		quirks = N2BT_Q_DEFAULT & ~N2BT_Q_LONGLOCAL;
		CHECK(!n2sk_answer_here(wln, sizeof(wln)) && !n2sk_answer_here(weir, sizeof(weir)), "0x0c13/0x0c52 forwarded without LONGLOCAL");
		quirks = 0;
		CHECK(!n2sk_answer_here(wed, 5), "0x0c5b forwarded with quirks 0");
		CHECK(!n2sk_answer_here(wln, sizeof(wln)), "0x0c13 forwarded with quirks 0");
		uint8_t wmt2[] = { 4, 0xe4, 5, 2, 6, 1, 0, 0 };
		CHECK(n2sk_rewrite(wmt2, 8) == 8, "raw with quirks 0");
		quirks = N2BT_Q_DEFAULT;
	}
	/* reassembly: 3000 random controller packets with junk bytes in between, delivered in random bursts */
	{
		srand(7);
		uint8_t exp[3000][300]; unsigned explen[3000], junk = 0;
		for (int i = 0; i < 3000; i++) {
			int t = rand() % 3; unsigned n;
			if (t == 0) { n = 3 + (unsigned)(rand() % 250); exp[i][0] = 4; exp[i][1] = (uint8_t)rand(); exp[i][2] = (uint8_t)(n - 3); }
			else if (t == 1) { n = 5 + (unsigned)(rand() % 290); exp[i][0] = 2; exp[i][1] = 1; exp[i][2] = 0x20; exp[i][3] = (uint8_t)(n - 5); exp[i][4] = (uint8_t)((n - 5) >> 8); }
			else { n = 4 + (unsigned)(rand() % 60); exp[i][0] = 3; exp[i][1] = 6; exp[i][2] = 0; exp[i][3] = (uint8_t)(n - 4); }
			for (unsigned k = (t == 0 ? 3 : t == 1 ? 5 : 4); k < n; k++) exp[i][k] = (uint8_t)rand();
			explen[i] = n;
		}
		/* fill the FIFO model progressively */
		unsigned next = 0, got = 0, bad = 0;
		uint8_t out[600];
		while (got < 3000) {
			while (next < 3000 && fifo_tail - fifo_head < 4000) {
				if (rand() % 50 == 0) { fifo[fifo_tail++] = 0x77; junk++; }   /* line noise between packets */
				memcpy(fifo + fifo_tail, exp[next], explen[next]); fifo_tail += explen[next]; next++;
			}
			int r = n2bt_hci_receive(out, sizeof(out), 5);
			if (r > 0) { if ((unsigned)r != explen[got] || memcmp(out, exp[got], (size_t)r)) bad++; got++; }
			if (fifo_tail > (1u << 16) - 1000) { memmove(fifo, fifo + fifo_head, fifo_tail - fifo_head); fifo_tail -= fifo_head; fifo_head = 0; }
		}
		CHECK(bad == 0, "reassembly: %u of 3000 packets wrong (junk %u, resync %u)", bad, junk, n2bt_st.rx_resync);
		CHECK(n2bt_st.rx_resync == junk, "resync count %u == junk %u", n2bt_st.rx_resync, junk);
	}
	/* bt13: every kernel answer of the real bt31a init is byte-identical with the default quirks; targeted pad only for fce2 0d/2d/05/0a */
	{
		unsigned same = 0, n = sizeof(bt31a_vsc) / sizeof(bt31a_vsc[0]);
		quirks = N2BT_Q_DEFAULT;
		for (unsigned i = 0; i < n; i++) {
			delivered_n = 0;
			bool here = n2sk_answer_here(bt31a_vsc[i].c, bt31a_vsc[i].cn);
			if (here && delivered_n == bt31a_vsc[i].rn && memcmp(delivered, bt31a_vsc[i].r, delivered_n) == 0) same++;
			else fprintf(stderr, "  differs: %02x%02x/%02x got %u bytes want %u\n", bt31a_vsc[i].c[2], bt31a_vsc[i].c[1], bt31a_vsc[i].c[4], delivered_n, bt31a_vsc[i].rn);
		}
		CHECK(same == n, "bt31a replay: %u of %u kernel answers byte-identical", same, n);
		uint8_t e9b[] = { 1, 0xe9, 0xfc, 5, 1, 0x27, 0x10, 0x10, 0 };
		quirks = N2BT_Q_DEFAULT | N2BT_Q_VSC_PAD;
		CHECK(n2sk_answer_here(e9b, 9) && delivered_n == 23, "blanket pad (0x400) reproduces bt32a's 23-byte fce9 reply");
		quirks = N2BT_Q_DEFAULT;
		CHECK(n2sk_answer_here(e9b, 9) && delivered_n == 7, "default: fce9 01 27 10 10 -> 7 bytes as bt31a");
		static const uint8_t subs[] = { 0x0d, 0x2d, 0x05, 0x0a, 0x0b, 0x1c }, need[] = { 8, 4, 1, 1, 0, 0 };
		for (unsigned i = 0; i < sizeof(subs); i++) {
			uint8_t c2[] = { 1, 0xe2, 0xfc, 5, subs[i], 0, 0, 0, 0 };
			CHECK(n2sk_answer_here(c2, 9) && delivered_n == 7u + need[i], "fce2 sub %#x -> %u data bytes", subs[i], need[i]);
		}
		quirks = N2BT_Q_DEFAULT & ~N2BT_Q_VSC_TARGET;
		uint8_t c3[] = { 1, 0xe2, 0xfc, 5, 0x0d, 0, 0, 0, 0 };
		CHECK(n2sk_answer_here(c3, 9) && delivered_n == 7, "targeted pad off -> fce2 0d empty (bt31a behaviour)");
		quirks = N2BT_Q_DEFAULT;
	}
	/* bt17: ACL credit / handle / length gate */
	{
		static struct n2sk_acl a; n2sk_acl_init(&a);
		uint8_t rbs[] = { 4, 0x0e, 0x0b, 1, 0x05, 0x10, 0, 0xab, 0x02, 0xb8, 6, 0, 1, 0 };   /* ACL 0x2ab x 6 */
		CHECK(n2sk_acl_event(&a, rbs, sizeof(rbs)) == 1 && a.acl_max == 0x2ab && a.acl_bufs == 6, "Read Buffer Size parsed %u x %u", a.acl_max, a.acl_bufs);
		uint8_t lebs[] = { 4, 0x0e, 0x07, 1, 0x02, 0x20, 0, 0xfb, 0, 8 };
		CHECK(n2sk_acl_event(&a, lebs, sizeof(lebs)) == 1 && a.le_max == 251 && a.le_bufs == 8, "LE Read Buffer Size parsed");
		uint8_t acl[700] = { 2, 0x32, 0x20, 0x00, 0x01 };   /* 256 bytes on h 0x32 */
		CHECK(n2sk_acl_gate(&a, acl, 5 + 256) == 2, "ACL before the connection exists: drop");
		uint8_t cc[] = { 4, 0x03, 0x0b, 0, 0x32, 0, 1, 2, 3, 4, 5, 6, 1, 0 };
		(void)n2sk_acl_event(&a, cc, sizeof(cc));
		unsigned sent = 0;
		for (int i = 0; i < 10; i++) { if (n2sk_acl_gate(&a, acl, 5 + 256) == 0) { n2sk_acl_sent(&a, acl); sent++; } }
		CHECK(sent == 6 && n2sk_acl_gate(&a, acl, 5 + 256) == 1, "6 buffers: 6 sent, the 7th held (sent %u)", sent);
		uint8_t nocp[] = { 4, 0x13, 5, 1, 0x32, 0, 3, 0 };
		(void)n2sk_acl_event(&a, nocp, sizeof(nocp));
		CHECK(a.out_acl == 3 && n2sk_acl_gate(&a, acl, 5 + 256) == 0, "3 completed -> sending again (in flight %u)", a.out_acl);
		uint8_t big[] = { 2, 0x32, 0x20, 0xac, 0x02 };
		CHECK(n2sk_acl_gate(&a, big, 5) == 3, "longer than 0x2ab: drop");
		uint8_t nocp2[] = { 4, 0x13, 5, 1, 0x32, 0, 50, 0 };
		(void)n2sk_acl_event(&a, nocp2, sizeof(nocp2));
		CHECK(a.out_acl == 0 && a.out[0] == 0, "over-reported completions clamp at 0");
		uint8_t dc[] = { 4, 0x05, 4, 0, 0x32, 0, 0x13 };
		(void)n2sk_acl_event(&a, dc, sizeof(dc));
		CHECK(a.nh == 0 && n2sk_acl_gate(&a, acl, 5 + 256) == 2, "after disconnection: drop");
		uint8_t lecc[] = { 4, 0x3e, 0x13, 0x01, 0, 0x40, 0, 0, 0, 1, 2, 3, 4, 5, 6, 0, 0, 0, 0, 0, 0, 0 };
		(void)n2sk_acl_event(&a, lecc, sizeof(lecc));
		uint8_t leacl[] = { 2, 0x40, 0x20, 0xfb, 0 }, leacl2[] = { 2, 0x40, 0x20, 0xfc, 0 };
		CHECK(n2sk_acl_gate(&a, leacl, 5) == 0 && n2sk_acl_gate(&a, leacl2, 5) == 3, "LE link uses the LE limits");
		srand(19);
		for (int i = 0; i < 100000; i++) {
			unsigned n = (unsigned)(rand() % 40); uint8_t *p = malloc(n ? n : 1);
			for (unsigned k = 0; k < n; k++) p[k] = (uint8_t)rand();
			if (n) p[0] = rand() % 2 ? 4 : 2;
			if (p[0] == 4) (void)n2sk_acl_event(&a, p, n); else if (n2sk_acl_gate(&a, p, n) == 0 && n >= 5) n2sk_acl_sent(&a, p);
			free(p);
		}
		CHECK(a.nh <= N2SK_ACLH, "100000 fuzzed events/ACL through the gate");
	}
	/* bt15: L2CAP signalling + AVDTP decode on a synthetic A2DP set-up, then fuzz */
	{
		static struct n2sk_l2 st; char tx[200]; int r;
		memset(&st, 0, sizeof(st));
#define L2(dirc, cid, ...) ({ uint8_t pl[] = { __VA_ARGS__ }; uint8_t pk[64]; unsigned pn = (unsigned)sizeof(pl); pk[0] = 2; pk[1] = 0x32; pk[2] = 0x20; pk[3] = (uint8_t)(pn + 4); pk[4] = 0; pk[5] = (uint8_t)pn; pk[6] = 0; pk[7] = (uint8_t)(cid); pk[8] = (uint8_t)((cid) >> 8); memcpy(pk + 9, pl, pn); n2sk_l2cap_text(dirc, pk, 9 + pn, &st, tx, sizeof(tx)); })
		r = L2('o', 1, 0x02, 1, 4, 0, 0x19, 0, 0x40, 0); CHECK(r > 0 && strstr(tx, "conn req psm 0x19 scid 0x40"), "conn req: %s", tx);
		r = L2('i', 1, 0x03, 1, 8, 0, 0x50, 0, 0x40, 0, 0, 0, 0, 0); CHECK(r > 0 && strstr(tx, "conn rsp dcid 0x50 scid 0x40 result 0"), "conn rsp: %s", tx);
		CHECK(st.n == 1 && st.ch[0].lcid == 0x40 && st.ch[0].rcid == 0x50, "channel learned lcid %#x rcid %#x", st.ch[0].lcid, st.ch[0].rcid);
		r = L2('o', 0x50, 0x10, 0x01); CHECK(r > 0 && strstr(tx, "AVDTP discover cmd"), "avdtp discover: %s", tx);
		r = L2('i', 0x40, 0x12, 0x01, 0x04, 0x08); CHECK(r > 0 && strstr(tx, "AVDTP discover accept"), "avdtp accept: %s", tx);
		r = L2('o', 0x50, 0x20, 0x03, 4, 8, 1, 0); CHECK(r > 0 && strstr(tx, "set-config cmd"), "set-config: %s", tx);
		r = L2('o', 0x50, 0x30, 0x07, 4); CHECK(r > 0 && strstr(tx, "start cmd"), "start: %s", tx);
		r = L2('o', 1, 0x02, 2, 4, 0, 0x19, 0, 0x41, 0); r = L2('i', 1, 0x03, 2, 8, 0, 0x51, 0, 0x41, 0, 0, 0, 0, 0);
		r = L2('o', 0x51, 0x80, 0x60, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0x9c);
		CHECK(r > 0 && strstr(tx, "media packet 1") && strstr(tx, " 80 60") && st.kind == 1, "media first packet dumped: %s", tx);
		r = L2('o', 0x51, 0x80, 0x60, 0, 2); r = L2('o', 0x51, 0x80, 0x60, 0, 3);
		r = L2('o', 0x51, 0x80, 0x60, 0, 4); CHECK(r == 0 && st.ch[1].tx > 0, "after 3 dumps media packets are counted, not logged");
		/* bt18: codec capabilities and configuration */
		r = L2('i', 0x40, 0x12, 0x02, 0x01, 0x00, 0x07, 0x08, 0x00, 0x02, 0x80, 0x01, 0x8c, 0x84, 0xe2, 0x00);
		CHECK(r > 0 && strstr(tx, "get-caps accept") && strstr(tx, "AAC obj 0x80 (MPEG2-LC)") && strstr(tx, "(44.1k)") && strstr(tx, "bitrate 320000") && strstr(tx, "vbr 1"),
		    "AAC capability decoded: %s", tx);
		r = L2('o', 0x50, 0x20, 0x03, 0x04, 0x04, 0x01, 0x00, 0x07, 0x06, 0x00, 0x00, 0x21, 0x15, 0x02, 0x35);
		CHECK(r > 0 && strstr(tx, "set-config cmd") && strstr(tx, "acp 1 int 1") && strstr(tx, "SBC freq 0x2 ch-mode 0x1") && strstr(tx, "bitpool 2-53"), "SBC configuration decoded: %s", tx);
		r = L2('i', 0x40, 0x12, 0x02, 0x07, 0x0a, 0x00, 0xff, 0x2d, 0x01, 0, 0, 0xaa, 0, 0x3c, 0x07);
		CHECK(r > 0 && strstr(tx, "vendor 0x12d codec 0xaa (LDAC)"), "vendor codec decoded: %s", tx);
		r = L2('i', 0x40, 0x13, 0x03, 0x07, 0x29); CHECK(r > 0 && strstr(tx, "set-config reject") && strstr(tx, "error 0x29"), "reject decoded: %s", tx);
		{
			uint8_t pk[] = { 2, 0x32, 0x20, 16, 0, 12, 0, 0x40, 0, 0x12, 0x02, 0x07, 0x08, 0x00, 0x02, 0x80, 0x01, 0x8c, 0x84, 0xe2, 0x00 };
			CHECK(n2sk_avdtp_filter(&st, 'i', pk, sizeof(pk), 0) == 0, "filter off by default changes nothing");
			uint8_t sbc[] = { 2, 0x32, 0x20, 12, 0, 8, 0, 0x40, 0, 0x12, 0x02, 0x07, 0x06, 0x00, 0x00, 0x21, 0x15, 0x02, 0x35 };
			CHECK(n2sk_avdtp_filter(&st, 'i', sbc, sizeof(sbc), 2) == 0 && sbc[14] == 0x00, "SBC never hidden");
			CHECK(n2sk_avdtp_filter(&st, 'i', pk, sizeof(pk), 1) == 1 && pk[14] == 0xff && pk[15] == 0xff && pk[20] == 0xff, "diagnostic 1 hides AAC as an unknown vendor codec, same length");
		}
		r = L2('i', 1, 0x06, 3, 4, 0, 0x40, 0, 0x50, 0); CHECK(r > 0 && strstr(tx, "disconn req"), "disconn: %s", tx);
		n2sk_l2_forget(&st, 0x32); CHECK(st.n == 0, "link forgotten");
		srand(17);
		for (int i = 0; i < 200000; i++) {
			unsigned n = (unsigned)(rand() % 64); uint8_t *p = malloc(n ? n : 1);
			for (unsigned k = 0; k < n; k++) p[k] = (uint8_t)rand();
			if (n) p[0] = 2;
			if (n > 8 && rand() % 2) { p[7] = (uint8_t)(rand() % 3 ? 1 : 0x40 + rand() % 4); p[8] = 0; }
			(void)n2sk_l2cap_text(rand() % 2 ? 'i' : 'o', p, n, &st, tx, sizeof(tx));
			if (st.n == N2SK_L2CH) st.n = 0;
			free(p);
		}
		CHECK(1, "200000 fuzzed ACL packets through the L2CAP decoder");
	}
	/* bt8: the pairing / A2DP sequence of a classic headset through the FIFO model, the reassembly, the counters and the event lines */
	{
		static uint8_t seq[64][1100]; unsigned sl[64], ns = 0;
#define EV(code, ...) do { uint8_t body[] = { __VA_ARGS__ }; seq[ns][0] = 4; seq[ns][1] = code; seq[ns][2] = (uint8_t)sizeof(body); memcpy(&seq[ns][3], body, sizeof(body)); sl[ns++] = 3 + (unsigned)sizeof(body); } while (0)
#define EVN(code, n) do { seq[ns][0] = 4; seq[ns][1] = code; seq[ns][2] = (uint8_t)(n); for (unsigned z = 0; z < (n); z++) seq[ns][3 + z] = (uint8_t)(z * 7); sl[ns++] = 3 + (n); } while (0)
#define ACL(h, n) do { seq[ns][0] = 2; seq[ns][1] = (uint8_t)(h); seq[ns][2] = (uint8_t)(0x20 | ((h) >> 8)); seq[ns][3] = (uint8_t)(n); seq[ns][4] = (uint8_t)((n) >> 8); for (unsigned z = 0; z < (n); z++) seq[ns][5 + z] = (uint8_t)z; sl[ns++] = 5 + (n); } while (0)
		EVN(0x2f, 255);                                            /* extended inquiry result (FreeBuds, EIR 240 bytes) */
		EVN(0x22, 15);                                             /* inquiry result with RSSI */
		EV(0x01, 0);                                               /* inquiry complete */
		EV(0x0f, 0, 1, 0x05, 0x04);                                /* command status: create connection */
		EV(0x03, 0, 0x0b, 0, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0, 1, 0);   /* connection complete h 0x0b */
		EV(0x1b, 0x0b, 0, 5);                                      /* max slots change */
		EV(0x0b, 0, 0x0b, 0, 0xbf, 0xfe, 0xcf, 0xfe, 0xdb, 0xff, 0x7b, 0x87);
		EVN(0x07, 255);                                            /* remote name request complete (248-byte name) */
		EV(0x17, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0);              /* link key request */
		EV(0x31, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0);              /* IO capability request */
		EV(0x32, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0, 3, 0, 2);     /* IO capability response: NoInputNoOutput */
		EV(0x33, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0, 1, 2, 3, 4); /* user confirmation request */
		EV(0x36, 0, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0);           /* simple pairing complete */
		EVN(0x18, 23);                                             /* link key notification */
		EV(0x06, 0, 0x0b, 0);                                      /* authentication complete */
		EV(0x08, 0, 0x0b, 0, 1);                                   /* encryption change on */
		ACL(0x0b, 12); ACL(0x0b, 16);                              /* L2CAP / AVDTP signalling */
		for (int i = 0; i < 20; i++) ACL(0x0b, 1021);              /* A2DP media sized (if the controller echoes / sends big ACL) */
		EV(0x13, 1, 0x0b, 0, 8, 0);                                /* number of completed packets */
		EV(0x14, 0, 0x0b, 0, 2, 0x20, 0);                          /* mode change: sniff */
		EV(0x2c, 0, 0x0c, 0, 0x24, 0x43, 0x5a, 0x10, 0x28, 0xe0, 2, 12, 2, 60, 0, 60, 0, 3);  /* eSCO connected */
		for (int i = 0; i < 6; i++) { seq[ns][0] = 3; seq[ns][1] = 0x0c; seq[ns][2] = 0; seq[ns][3] = 60; memset(&seq[ns][4], 0x55, 60); sl[ns++] = 64; }
		EV(0x05, 0, 0x0c, 0, 0x16);                                /* disconnection complete */
		EV(0xff, 0x50, 1, 2, 3);                                   /* MediaTek vendor event */
		EV(0x10, 0x23);                                            /* hardware error */
		EV(0x3e, 0x0a, 0, 0x40, 0, 0, 0);                          /* LE enhanced connection complete (cut short by us: still >= 5 bytes) */
		unsigned got = 0, bad = 0, lines = 0, shorts = 0;
		struct n2sk_rt rt; memset(&rt, 0, sizeof(rt));
		uint8_t out[2048]; char tx[120];
		fifo_head = fifo_tail = 0;
		for (unsigned i = 0; i < ns; i++) { memcpy(fifo + fifo_tail, seq[i], sl[i]); fifo_tail += sl[i]; }
		for (int tries = 0; got < ns && tries < 100000; tries++) {
			int r = n2bt_hci_receive(out, sizeof(out) - 80, 5);
			if (r <= 0) continue;
			if ((unsigned)r != sl[got] || memcmp(out, seq[got], (size_t)r)) bad++;
			n2sk_count('i', out, (unsigned)r, &rt);
			int tl = n2sk_event_text(out, (unsigned)r, tx, sizeof(tx));
			if (tl > 0) { lines++; if (strstr(tx, "too short")) shorts++; CHECK(tl < (int)sizeof(tx) + 64, "line length"); }
			(void)n2sk_rewrite(out, (unsigned)r);
			got++;
		}
		CHECK(got == ns && bad == 0, "pairing sequence: %u of %u packets, %u wrong", got, ns, bad);
		CHECK(rt.acl_rx == 22 && rt.acl_rx_max == 1026 && rt.sco_rx == 6 && rt.nocp == 1 && rt.inq == 2, "counters acl %u max %u sco %u nocp %u inq %u", rt.acl_rx, rt.acl_rx_max, rt.sco_rx, rt.nocp, rt.inq);
		CHECK(lines == 18 && shorts == 0, "event lines %u (expected 18), too-short %u", lines, shorts);
		CHECK(n2sk_cmd_noteworthy(0x0405) && n2sk_cmd_noteworthy(0x042c) && n2sk_cmd_noteworthy(0x0811) && !n2sk_cmd_noteworthy(0x0c13) && !n2sk_cmd_noteworthy(0xfce9), "noteworthy commands");
	}
	/* bt8 fuzz: event lines, answers and rewrites on exact-size heap copies (ASan sees any read past the packet) */
	{
		srand(11);
		char tx[120]; unsigned over = 0;
		for (int i = 0; i < 300000; i++) {
			unsigned n = (unsigned)(rand() % 300);
			uint8_t *p = malloc(n ? n : 1);
			for (unsigned k = 0; k < n; k++) p[k] = (uint8_t)rand();
			if (n > 0 && rand() % 4) p[0] = 4;
			if (n > 2 && rand() % 2) p[2] = (uint8_t)(n - 3 + (rand() % 5) - 2);
			if (n > 1 && rand() % 3 == 0) { static const uint8_t codes[] = { 1, 3, 4, 5, 6, 7, 8, 0x0b, 0x0c, 0x10, 0x12, 0x14, 0x17, 0x18, 0x1a, 0x2c, 0x2d, 0x30, 0x31, 0x32, 0x33, 0x34, 0x36, 0x59, 0x3e, 0xe4, 0xff, 0x0e, 0x0f, 0x13 }; p[1] = codes[rand() % (int)sizeof(codes)]; }
			int tl = n2sk_event_text(p, n, tx, sizeof(tx));
			if (tl >= (int)sizeof(tx)) over++;
			struct n2sk_rt rt; memset(&rt, 0, sizeof(rt)); n2sk_count(rand() % 2 ? 'i' : 'o', p, n, &rt);
			uint16_t op; uint8_t st; (void)n2sk_status_of(p, n, &op, &st);
			(void)n2sk_rewrite(p, n);
			if (n > 0) { p[0] = 1; (void)n2sk_answer_here(p, n); }
			free(p);
		}
		CHECK(1, "300000 fuzzed packets parsed without a read past the end");
		(void)over;
	}
	/* bt8 fuzz: garbage + valid packets through the BTIF reassembly with the bridge's room (1968) */
	{
		srand(13);
		unsigned pkts = 0, maxhave = 0, toolong = 0;
		uint8_t out[1968];
		fifo_head = fifo_tail = 0; n2bt_rx_have = 0;
		for (int round = 0; round < 400; round++) {
			if (fifo_tail > (1u << 16) - 5000) { memmove(fifo, fifo + fifo_head, fifo_tail - fifo_head); fifo_tail -= fifo_head; fifo_head = 0; }
			unsigned n = 1 + (unsigned)(rand() % 3000);
			if (fifo_tail + n > sizeof(fifo)) n = 0;
			for (unsigned k = 0; k < n; k++) fifo[fifo_tail++] = (uint8_t)(rand() % 8 == 0 ? 2 + rand() % 4 : rand());
			for (int k = 0; k < 50; k++) {
				int r = n2bt_hci_receive(out, sizeof(out), 1);
				if (r > 0) { pkts++; if ((unsigned)r > sizeof(out)) toolong++; }
				if (n2bt_rx_have > maxhave) maxhave = n2bt_rx_have;
			}
		}
		CHECK(toolong == 0 && maxhave <= 4096u, "garbage stream: %u packets, none longer than the room, reassembly <= 4096 (%u)", pkts, maxhave);
	}
	printf("x");
#undef printf
	printf("t_bt: %d checks, %d failures%s\n", checks, fails, fails ? "" : " -- ALL OK");
	return fails != 0;
}
