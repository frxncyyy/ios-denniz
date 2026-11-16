/*
 * 0296: hands-free testing over the USB console (gpu/build/autotest, host tool phone.py).
 *
 * Synthetic fingers and keys enter iOS where the real ones do: sweet_hid_touch() / sweet_hid_key() (SweetHID.cpp), in panel
 * pixels (1080x2400). Fingers 8 and 9 are used, the FT3518 thread reports the low ones first. A picture of the panel is taken
 * by SweetFramebuffer.cpp (sweet_fb_snap: the buffer the overlay reads, packed R,G,B) and read back in 1024-byte pieces.
 *
 * Transport: vendor IN requests on EP0 of "Nord2 XNU console" (sweet_usb.c calls nat_usb where it called nal_usb; the audio
 * mailbox is asked first here). No OUT data stage exists, so parameters go in sixteen 16-bit registers:
 *   0x70 status   -> 20 words (nat_usb below)
 *   0x71 register    wIndex = number (0..15), wValue = value -> the value
 *   0x72 command     wValue = command, wIndex = argument     -> {accepted, token}
 *   0x73 picture     wValue | wIndex << 16 = byte offset     -> up to 1024 bytes
 * Commands (one at a time, run by nat_thread; a second one while busy is refused, except ABORT):
 *   1 gesture  arg bits 0-1 fingers (1/2), bit 8 keep down at the end, bit 9 already down (no press)
 *              r0..r3 finger A x0,y0,x1,y1; r4..r7 finger B; r8 move ms; r9 hold ms before; r10 hold ms after
 *   2 key      arg 0 power, 1 volume up, 2 volume down, 3 menu; r8 hold ms
 *   3 picture  arg bits 0-3 = scale (1..8: every scale-th pixel of every scale-th row), bit 4 = run-length packed (below);
 *              r11..r14 = x, y, width, height of the part wanted in panel pixels (width 0: all of it)
 *   4 abort    lifts the synthetic fingers and keys, ends the running command
 *   5 reset    arg must be 0x4642: file systems synced, then the TOPRGU reset of sweet_amfi.c's sweet_reset_now(). The RTC
 *              fast-boot bit is armed at every boot (NORD2 AUTOFB), so the phone comes back in LK fastboot.
 *   6 log rewind  arg | r15 << 16 = the size the host has read up to. The console's live log (sweet_early.c, 4 MiB, stops when
 *              full) starts again at 0 if its size is still exactly that; result 5 = it grew meanwhile (read on, ask again),
 *              6 = this kernel's sweet_early.o is not the one whose code is known here (nothing is touched).
 *   7 hwcodec  (0298, nord2_hwcodec.inc) arg = step: 0 status, 1 VEN power + clocks, 2 larb7 + reset, 3 JPEG encode (r0 width,
 *              r1 height, r2 quality code; r0 = 0: 640x480 Q90), 4 off. Result 1 = done, 16.. = what stopped it. After a good
 *              encode the JPEG is on the picture channel: status word 10 (snap_info[2]) = its size (the "front after" byte reads 0x4a).
 *              0312: step 4 is refused with result 30 while /dev/redwood-vcodec, /dev/redwood-jpeg or command 8 hold VEN.
 *   8 vcodec   (0312, nord2_vcodec.inc n2vc_at_command) arg bits 0-7 = step: 0 status (read only), 1 VENC power + clocks + larb,
 *              2 larb read-back + interrupt status, 3 VENC registers (r0 offset, r1 = 1 write r2 + read back + restore),
 *              4 off; arg bit 8 = VENC core 1 too. Result 1 = done, 16.. = what stopped it (host tool gpu/hwcodec/tools/hwv.py).
 * 0640 (snap-20261114): the picture is the overlay plan on the panel composed in software (SweetFramebuffer.cpp snapHw); refusals
 *   20 = kern.n2snap 0, 21 = no overlay frame shown yet, 22 = no memory (4 = other).  Status: asked with wLength >= 128 it has 32
 *   words; 20..31 = 0640 state (fb flags: bit 0 iOS display off, 1 DCS display off sent = panel dark, 2 overlay plan on, 3 band
 *   rotation live, 4 framebuffer present, 8..15 last snapshot flags, 16..31 last snapshot ms; DBV sent | target << 16; brightness
 *   request nits 16.16; presents; HID: injected touch reports, injected downs | key reports << 16, all touch reports, touch clients |
 *   button clients << 16, last injected x | y << 16, injected reports with no client; 30 = 0; 31 = 0x534e0001).  wLength 80: as before.
 * Packed pictures: a byte c, then c < 128: c + 1 pixels as they are; c >= 128: one pixel shown c - 126 times.
 * The USB side only stores words and copies bytes; everything else runs on the thread.
 */
extern void *sweet_fb_snap(const uint32_t want[6], uint32_t info[8]);
extern void sweet_fb_status_words(uint32_t w[4]);      /* 0640 SweetFramebuffer.cpp: panel state, brightness, presents */
extern void sweet_hid_counts(uint32_t c[6]);           /* 0640 hid/SweetHID.cpp: dispatched HID reports */
extern void nord2_log_stream_info(uint32_t info[2]);
extern int nal_usb(const uint8_t setup[8], uint8_t *, size_t, uint32_t *length);
int nat_usb(const uint8_t setup[8], uint8_t *out, size_t capacity, uint32_t *length);

#define NAT_MAGIC   0x5441324eu     /* "N2AT" */
#define NAT_VERSION 2u
enum { NAT_USB_STATUS = 0x70, NAT_USB_REG = 0x71, NAT_USB_CMD = 0x72, NAT_USB_DATA = 0x73 };
enum { NAT_CMD_GESTURE = 1, NAT_CMD_KEY = 2, NAT_CMD_SNAP = 3, NAT_CMD_ABORT = 4, NAT_CMD_RESET = 5, NAT_CMD_LOGREWIND = 6, NAT_CMD_HWCODEC = 7, NAT_CMD_VCODEC = 8 };
extern int sync_internal(void);         /* bsd/vfs/vfs_syscalls.c */
enum { NAT_FINGER = 8, NAT_REGS = 16 };

static struct {
	uint32_t reg[NAT_REGS];             /* USB side */
	uint32_t job[NAT_REGS], cmd, arg;   /* written by the USB side only while pending == 0 */
	uint32_t pending, abort;
	uint32_t token, done, result;
	uint32_t started, gestures, keys, snaps, refused;
	uint32_t down;                      /* synthetic fingers down (bit 0 A, bit 1 B), key down + 1 in bits 8.. */
	uint32_t snap_seq, snap_ready, snap_info[8];
	uint32_t boot, rewinds, log_known;     /* log_known: 0 not looked at, 1 found, 2 not this sweet_early.o */
	uint32_t *log_size;
	const uint8_t *snap;
} nat;
static int nat_chan;

static uint32_t
nat_get(const uint32_t *p)
{
	return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

static void
nat_put(uint32_t *p, uint32_t v)
{
	__atomic_store_n(p, v, __ATOMIC_SEQ_CST);
}

static uint64_t
nat_ms(void)
{
	uint64_t ns;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return ns / 1000000u;
}

static void
nat_wait(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&nat_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

/* false: aborted */
static bool
nat_hold(uint32_t ms)
{
	uint64_t end = nat_ms() + ms;
	while (nat_ms() < end) {
		if (nat_get(&nat.abort)) { return false; }
		uint64_t left = end - nat_ms();
		nat_wait(left > 8 ? 8 : (unsigned)(left ? left : 1));
	}
	return !nat_get(&nat.abort);
}

static void
nat_lift(void)
{
	for (unsigned f = 0; f < 2; f++) {
		if (nat.down & (1u << f)) {
			sweet_hid_touch((int)(NAT_FINGER + f), 0, (int)nat.job[f * 4 + 2], (int)nat.job[f * 4 + 3]);
		}
	}
	if (nat.down >> 8) { sweet_hid_key((int)(nat.down >> 8) - 1, 0); }
	nat.down = 0;
}

static uint32_t
nat_gesture(uint32_t arg)
{
	unsigned n = arg & 3;
	const uint32_t *r = nat.job;
	if (n < 1 || n > 2) { return 2; }
	for (unsigned f = 0; f < n; f++) {
		if (r[f * 4] >= 1080 || r[f * 4 + 1] >= 2400 || r[f * 4 + 2] >= 1080 || r[f * 4 + 3] >= 2400) { return 2; }
	}
	if (!(arg & 0x200)) {
		for (unsigned f = 0; f < n; f++) {
			sweet_hid_touch((int)(NAT_FINGER + f), 1, (int)r[f * 4], (int)r[f * 4 + 1]);
			nat.down |= 1u << f;
		}
	}
	bool ok = nat_hold(r[9]);
	uint64_t t0 = nat_ms(), dur = r[8];
	while (ok && dur) {
		uint64_t t = nat_ms() - t0;
		if (t >= dur) { break; }
		for (unsigned f = 0; f < n; f++) {
			int x = (int)r[f * 4] + (int)(((int64_t)r[f * 4 + 2] - (int64_t)r[f * 4]) * (int64_t)t / (int64_t)dur);
			int y = (int)r[f * 4 + 1] + (int)(((int64_t)r[f * 4 + 3] - (int64_t)r[f * 4 + 1]) * (int64_t)t / (int64_t)dur);
			sweet_hid_touch((int)(NAT_FINGER + f), 1, x, y);
		}
		ok = nat_hold(8);
	}
	if (ok) {
		for (unsigned f = 0; f < n; f++) {
			sweet_hid_touch((int)(NAT_FINGER + f), 1, (int)r[f * 4 + 2], (int)r[f * 4 + 3]);
		}
		ok = nat_hold(r[10]);
	}
	if (!ok || !(arg & 0x100)) { nat_lift(); }
	nat.gestures++;
	return ok ? 1 : 3;
}

static uint32_t
nat_key(uint32_t arg)
{
	if (arg > 3) { return 2; }
	sweet_hid_key((int)arg, 1);
	nat.down |= (arg + 1) << 8;
	bool ok = nat_hold(nat.job[8] ? nat.job[8] : 80);
	nat_lift();
	nat.keys++;
	return ok ? 1 : 3;
}

static uint32_t
nat_snap(uint32_t arg)
{
	uint32_t info[8] = {0};
	nat_put(&nat.snap_ready, 0);
	const uint32_t want[6] = {arg & 15, (arg >> 4) & 1, nat.job[11], nat.job[12], nat.job[13], nat.job[14]};
	const uint8_t *p = (const uint8_t *)sweet_fb_snap(want, info);
	if (p == NULL) { nat_put(&nat.snap_info[7], info[7]); return info[3] >= 20 && info[3] <= 22 ? info[3] : 4; }      /* 0640 reasons */
	for (unsigned i = 0; i < 8; i++) { nat_put(&nat.snap_info[i], info[i]); }
	nat.snap = p;
	nat_put(&nat.snap_seq, nat_get(&nat.snap_seq) + 1);
	nat_put(&nat.snap_ready, 1);
	nat.snaps++;
	return 1;
}

/*
 * The live log's size word is static in sweet_early.c, an object of the startup archive that is never recompiled. Its accessor
 * nord2_log_stream_info() is exactly: mov w8,#0x400000; adrp x9; add x9,x9,#off; str w8,[x0]; ldar w8,[x9]; str w8,[x0,#4]; ret.
 * The address is taken from those two instructions only if all seven match, and only if the word there reads what the accessor
 * returns, twice.
 */
static uint32_t
nat_log_rewind(uint32_t expected)
{
	if (!nat.log_known) {
		const uint32_t *c = (const uint32_t *)(uintptr_t)&nord2_log_stream_info;
		nat.log_known = 2;
		if (c[0] == 0x52a00808u && (c[1] & 0x9f00001fu) == 0x90000009u && (c[2] & 0xffc003ffu) == 0x91000129u &&
		    c[3] == 0xb9000008u && c[4] == 0x88dffd28u && c[5] == 0xb9000408u && c[6] == 0xd65f03c0u) {
			int64_t imm = (int64_t)((((uint64_t)(c[1] >> 5) & 0x7ffffu) << 2) | ((c[1] >> 29) & 3u));
			if (imm & (1ll << 20)) { imm -= 1ll << 21; }
			uint32_t *p = (uint32_t *)((((uintptr_t)&c[1]) & ~(uintptr_t)0xfff) + (uintptr_t)(imm * 4096) + ((c[2] >> 10) & 0xfffu));
			uint32_t a[2], b[2];
			nord2_log_stream_info(a);
			uint32_t seen = __atomic_load_n(p, __ATOMIC_SEQ_CST);
			nord2_log_stream_info(b);
			if (a[0] == 0x400000u && seen >= a[1] && seen <= b[1] && b[1] <= 0x400000u) {
				printf("NORD2 AUTOTEST: live log size word found (%u bytes now)\n", seen);
				nord2_log_stream_info(a);
				seen = __atomic_load_n(p, __ATOMIC_SEQ_CST);
				nord2_log_stream_info(b);
				if (seen >= a[1] && seen <= b[1] && b[1] > 0) { nat.log_size = p; nat.log_known = 1; }
			}
		}
	}
	if (nat.log_known != 1) { return 6; }
	uint32_t want = expected;
	if (!__atomic_compare_exchange_n(nat.log_size, &want, 0, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) { return 5; }
	nat.rewinds++;
	return 1;
}

static void
nat_reset(void)
{
	volatile uint32_t *wdt = (volatile uint32_t *)ml_io_map(0x10007000UL, 0x1000);
	printf("NORD2 AUTOTEST: reset asked over USB (fastboot if NORD2 AUTOFB armed the RTC bit at boot)\n");
	nat_lift();
	(void)sync_internal();
	nat_wait(2000);
	(void)sync_internal();
	nat_wait(4000);                     /* the console drains, the metadata reaches its home blocks */
	if (wdt == NULL) { return; }
	wdt[0] = (wdt[0] & ~(1U << 3)) | 0x22000000U;
	for (;;) {
		wdt[0x14 / 4] = 0x1209;
		nat_wait(5);
	}
}

/* 0298: one step of the hardware JPEG bring-up; a finished JPEG is handed to the picture channel (0x73) */
static uint32_t
nat_hwcodec(uint32_t step)
{
	const uint8_t *jpeg = NULL;
	uint32_t bytes = 0;
	nat_put(&nat.snap_ready, 0);
	if (step == 4 && n2vc_ven_refs() != 0) {                 /* 0312: n2j_off() would pull VEN from under the vcodec driver */
		printf("NORD2 JPG: step 4 refused: VEN is held by the vcodec/jpeg drivers (%u references)\n", n2vc_ven_refs());
		return 30;
	}
	uint32_t result = n2j_command(step, nat.job, &jpeg, &bytes);
	if (jpeg != NULL && bytes) {
		const uint32_t info[8] = {nat.job[0] ? nat.job[0] : 640, nat.job[0] ? nat.job[1] : 480, bytes, 1, 0, 0x4a, 0, 0};
		for (unsigned i = 0; i < 8; i++) { nat_put(&nat.snap_info[i], info[i]); }
		nat.snap = jpeg;
		nat_put(&nat.snap_seq, nat_get(&nat.snap_seq) + 1);
		nat_put(&nat.snap_ready, 1);
	}
	return result;
}

static void
nat_thread(void *arg, wait_result_t wr)
{
	(void)arg; (void)wr;
	printf("NORD2 AUTOTEST: ready (USB 0x70-0x73, fingers %u/%u)\n", NAT_FINGER, NAT_FINGER + 1);
	for (;;) {
		if (!nat_get(&nat.pending)) {
			if (nat_get(&nat.abort)) { nat_lift(); nat_put(&nat.abort, 0); }
			nat_wait(10);
			continue;
		}
		uint32_t result = 2;
		nat_put(&nat.abort, 0);
		switch (nat.cmd) {
		case NAT_CMD_GESTURE: result = nat_gesture(nat.arg); break;
		case NAT_CMD_KEY: result = nat_key(nat.arg); break;
		case NAT_CMD_SNAP: result = nat_snap(nat.arg); break;
		case NAT_CMD_RESET: if (nat.arg == 0x4642) { nat_reset(); } break;
		case NAT_CMD_LOGREWIND: result = nat_log_rewind(nat.arg | nat.job[15] << 16); break;
		case NAT_CMD_HWCODEC: result = nat_hwcodec(nat.arg); break;
		case NAT_CMD_VCODEC: result = n2vc_at_command(nat.arg, nat.job); break;   /* 0312 */
		default: break;
		}
		nat_put(&nat.result, result);
		nat_put(&nat.done, nat.token);
		nat_put(&nat.pending, 0);
	}
}

static void
nat_le32(uint8_t *out, uint32_t v)
{
	out[0] = (uint8_t)v; out[1] = (uint8_t)(v >> 8); out[2] = (uint8_t)(v >> 16); out[3] = (uint8_t)(v >> 24);
}

int
nat_usb(const uint8_t setup[8], uint8_t *out, size_t capacity, uint32_t *length)
{
	int r = nal_usb(setup, out, capacity, length);
	if (r != -1) { return r; }
	if (!setup || !out || !length) { return 0; }
	unsigned code = setup[1];
	if (code < NAT_USB_STATUS || code > NAT_USB_DATA) { return -1; }
	unsigned value = setup[2] | (unsigned)setup[3] << 8, index = setup[4] | (unsigned)setup[5] << 8;
	unsigned wanted = setup[6] | (unsigned)setup[7] << 8;
	*length = 0;
	if (setup[0] != 0xc0 || !wanted || capacity < 128 || !nat_get(&nat.started)) { return 0; }
	uint32_t n = 0;
	if (code == NAT_USB_STATUS) {
		if (value || index) { return 0; }
		const uint32_t v[20] = {NAT_MAGIC, NAT_VERSION, nat_get(&nat.pending), nat_get(&nat.token), nat_get(&nat.done),
			            nat_get(&nat.result), nat_get(&nat.snap_seq), nat_get(&nat.snap_ready),
			            nat_get(&nat.snap_info[0]), nat_get(&nat.snap_info[1]), nat_get(&nat.snap_info[2]),
			            nat_get(&nat.snap_info[3]) | nat_get(&nat.snap_info[4]) << 8 | nat_get(&nat.snap_info[5]) << 16 |
			            nat_get(&nat.snap_info[7]) << 24,
			            nat_get(&nat.snap_info[6]), nat.gestures | nat.keys << 16, nat.snaps | nat.refused << 16, nat.down,
			            nat.boot, nat.rewinds, nat.log_known, 0};
		for (unsigned i = 0; i < 20; i++) { nat_le32(out + i * 4, v[i]); }
		n = 80;
		if (wanted >= 128) {      /* 0640: panel / brightness / HID delivery words (cheap, lock-free reads) */
			uint32_t fw[4], hc[6];
			sweet_fb_status_words(fw);
			sweet_hid_counts(hc);
			const uint32_t x[12] = {fw[0], fw[1], fw[2], fw[3], hc[0], hc[1], hc[2], hc[3], hc[4], hc[5], 0, 0x534e0001u};
			for (unsigned i = 0; i < 12; i++) { nat_le32(out + 80 + i * 4, x[i]); }
			n = 128;
		}
	} else if (code == NAT_USB_REG) {
		if (index >= NAT_REGS) { return 0; }
		nat_put(&nat.reg[index], value);
		nat_le32(out, value);
		n = 4;
	} else if (code == NAT_USB_CMD) {
		uint32_t accepted = 0;
		if (value == NAT_CMD_ABORT) {
			nat_put(&nat.abort, 1);
			accepted = 1;
		} else if (((value >= NAT_CMD_GESTURE && value <= NAT_CMD_SNAP) || value == NAT_CMD_RESET || value == NAT_CMD_LOGREWIND || value == NAT_CMD_HWCODEC ||
		            value == NAT_CMD_VCODEC) && !nat_get(&nat.pending)) {
			for (unsigned i = 0; i < NAT_REGS; i++) { nat.job[i] = nat_get(&nat.reg[i]); }
			nat.cmd = value; nat.arg = index;
			nat.token++;
			nat_put(&nat.pending, 1);
			accepted = 1;
		} else {
			nat.refused++;
		}
		nat_le32(out, accepted);
		nat_le32(out + 4, nat.token);
		n = 8;
	} else {
		uint32_t offset = value | (uint32_t)index << 16, bytes = nat_get(&nat.snap_info[2]);
		if (!nat_get(&nat.snap_ready) || nat.snap == NULL || offset > bytes) { return 0; }
		n = bytes - offset;
		if (n > capacity) { n = (uint32_t)capacity; }
		if (n > wanted) { n = wanted; }
		memcpy(out, nat.snap + offset, n);
	}
	*length = n < wanted ? n : wanted;
	return 1;
}

static void
nord2_autotest_start(void)
{
	thread_t thread;
	if (nat_get(&nat.started)) { return; }
	nat.boot = (uint32_t)mach_absolute_time() | 1u;      /* tells one boot from the next for the host's log offset */
	if (kernel_thread_start(nat_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
		nat_put(&nat.started, 1);
	} else {
		printf("NORD2 AUTOTEST: thread start failed\n");
	}
}
