static unsigned
n2bt_h4_len(const uint8_t *b, unsigned have)
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

static int
n2bt_fw_check(const struct n2bt_fw *f, uint32_t *offset)
{
	const uint8_t *h = f->buf;
	uint32_t off, body;

	if (f->len < 48 + 64 || memcmp(h + 16, "ALPS", 4) != 0 || h[20] != 0x8a || h[27] != 0xf0) {
		return EINVAL;
	}
	off = ((uint32_t)h[26] << 16) | ((uint32_t)h[25] << 8);
	body = f->len - 48;
	if ((body & 3u) || off < f->emi_start || off + body > f->emi_start + f->emi_size) {
		return ERANGE;
	}
	*offset = off;
	return 0;
}

#define N2BT_RXBUF 4096u
static uint8_t n2bt_rx[N2BT_RXBUF]; static unsigned n2bt_rx_have;
static void
n2bt_rx_drain(void)
{
	while ((rd(BTIF_LSR) & 1u) && n2bt_rx_have < N2BT_RXBUF) {
		n2bt_rx[n2bt_rx_have++] = (uint8_t)(rd(BTIF_RBR_THR) & 0xff);
	}
	if (n2bt_rx_have == N2BT_RXBUF && (rd(BTIF_LSR) & 1u)) {
		n2bt_st.rx_overrun++;
	}
}

int
n2bt_hci_receive(uint8_t *h4, unsigned room, unsigned wait_ms)
{
	uint64_t deadline = 0, now;

	clock_interval_to_deadline(wait_ms, NSEC_PER_MSEC, &deadline);
	for (;;) {
		unsigned n;

		n2bt_rx_drain();
		while (n2bt_rx_have > 0 && (n2bt_rx[0] < 2 || n2bt_rx[0] > 5 || n2bt_rx[0] == 1)) {
			/* not a controller packet type: resync one byte at a time */
			memmove(n2bt_rx, n2bt_rx + 1, --n2bt_rx_have);
			n2bt_st.rx_resync++;
		}
		n = n2bt_h4_len(n2bt_rx, n2bt_rx_have);
		if (n != 0 && n > N2BT_RXBUF) {
			n2bt_rx_have = 0;
			n2bt_st.rx_resync++;
			continue;
		}
		if (n != 0 && n <= n2bt_rx_have) {
			if (n > room) {
				memmove(n2bt_rx, n2bt_rx + n, n2bt_rx_have - n);
				n2bt_rx_have -= n;
				return -EMSGSIZE;
			}
			memcpy(h4, n2bt_rx, n);
			memmove(n2bt_rx, n2bt_rx + n, n2bt_rx_have - n);
			n2bt_rx_have -= n;
			n2bt_st.rx_pkts++;
			n2bt_st.rx_bytes += n;
			return (int)n;
		}
		now = mach_absolute_time();
		if (now >= deadline) {
			return 0;
		}
		if (rd(BTIF_LSR) & 1u) {
			continue;
		}
		IODelay(100);
	}
}

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

static uint8_t n2sk_ev[300];
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

#define N2SK_CLOG 160u
static struct { uint16_t op; uint8_t st; char how; } n2sk_clog[N2SK_CLOG];
static unsigned n2sk_clog_n, n2sk_clog_over;
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

static bool
n2sk_status_of(const uint8_t *b, unsigned n, uint16_t *op, uint8_t *st)
{
	if (n >= 7 && b[0] == 4 && b[1] == 0x0e) { *op = (uint16_t)(b[4] | (b[5] << 8)); *st = b[6]; return *op != 0; }
	if (n >= 7 && b[0] == 4 && b[1] == 0x0f) { *op = (uint16_t)(b[5] | (b[6] << 8)); *st = b[3]; return *op != 0; }
	return false;
}

struct n2sk_rt {
	uint32_t acl_rx, acl_rx_b, acl_rx_max, acl_tx, acl_tx_b, acl_tx_max, sco_rx, sco_tx, iso, ev, nocp, inq, cmd;
};
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

static bool
n2sk_cmd_noteworthy(uint16_t op)
{
	return (op >> 10) == 1 || (op >> 10) == 2 || op == 0x200a || op == 0x200c || op == 0x200d || op == 0x2039 || op == 0x2042 || op == 0x2043;
}

#define N2SK_L2CH 16
#define N2SK_ACLH 8
struct n2sk_l2 {
	struct { uint16_t h, lcid, rcid, psm; uint8_t role, seen_i, seen_o, mpk; uint32_t rx, tx, last_tx; } ch[N2SK_L2CH];
	unsigned n;
	int kind;                                 /* bt18: last line: 0 other, 1 AVDTP (S11d), 2 config */
};
struct n2sk_acl {
	uint16_t h[N2SK_ACLH], out[N2SK_ACLH];
	uint8_t le[N2SK_ACLH];
	unsigned nh;
	uint16_t acl_max, le_max, acl_bufs, le_bufs;
	uint32_t out_acl, out_le, max_out, holds, drops, stall_resets, sent;
};
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

