#define N2BT_CRUMB_PA   0x4811e000u
#define N2BT_CRUMB_SIZE 0x1000u
#define N2BT_LIN_CAP    900u
#define N2BT_NOW_AT     900u
#define N2BT_NOW_LEN    40u
#define N2BT_GUARD_AT   940u
#define N2BT_GUARD_LEN  24u
#define N2BT_RING_AT    964u
#define N2BT_SLOT       76u
#define N2BT_SLOTS      41u
#define N2BT_DATA_END   (N2BT_RING_AT + N2BT_SLOT * N2BT_SLOTS)   /* 4080 <= 4096 - 12 */
static volatile uint8_t *n2bt_crumb_va;
static uint32_t n2bt_crumb_seq, n2bt_rearm, n2bt_prev_reset, n2bt_end_written;
static uint32_t n2bt_lin_at, n2bt_ring_next;
static void
n2bt_crumb_slot(uint32_t at, uint32_t len, const char *t, uint32_t n)
{
	if (n2bt_crumb_va == NULL || len == 0 || at + len > N2BT_DATA_END) return;
	for (uint32_t i = 0; i + 1 < len; i++) {
		char ch = i < n ? t[i] : ' ';
		n2bt_crumb_va[12u + at + i] = (uint8_t)(ch == '\n' ? ' ' : ch);
	}
	n2bt_crumb_va[12u + at + len - 1] = '\n';
	
}

static void
n2bt_crumb_raw(const char *t, uint32_t n)
{
	if (n2bt_crumb_va == NULL) return;
	if (n2bt_lin_at + n <= N2BT_LIN_CAP) {
		for (uint32_t i = 0; i < n; i++) n2bt_crumb_va[12u + n2bt_lin_at + i] = (uint8_t)t[i];
		n2bt_lin_at += n;
		
		return;
	}
	if (n2bt_lin_at < N2BT_LIN_CAP) {              /* close the linear part with blanks: no half line in it */
		char pad[1] = { ' ' };
		while (n2bt_lin_at < N2BT_LIN_CAP - 1) { n2bt_crumb_va[12u + n2bt_lin_at++] = (uint8_t)pad[0]; }
		n2bt_crumb_va[12u + n2bt_lin_at++] = '\n';
	}
	n2bt_crumb_slot(N2BT_RING_AT + N2BT_SLOT * (n2bt_ring_next % N2BT_SLOTS), N2BT_SLOT, t, n);
	n2bt_ring_next++;
}

void
n2bt_now(const char *step, uint32_t a, uint32_t b)
{
	char t[N2BT_NOW_LEN];
	uint64_t ns = 0;
	int k;
	if (n2bt_crumb_va == NULL) return;
	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	k = snprintf(t, sizeof(t), "N2BT NOW %llu %s %x %x", (unsigned long long)(ns / 1000000ull), step, a, b);
	if (k < 0) return;
	if (k > (int)sizeof(t) - 1) k = (int)sizeof(t) - 1;
	n2bt_crumb_slot(N2BT_NOW_AT, N2BT_NOW_LEN, t, (uint32_t)k);
}

static const char *
n2bt_find(const char *h, const char *n)
{
	size_t nl = strlen(n);
	for (; *h; h++) if (strncmp(h, n, nl) == 0) return h;
	return NULL;
}

static void n2bt_crumb(const char *fmt, ...);
static void
n2bt_crumb(const char *fmt, ...)
{
	char line[232];
	uint64_t ns = 0;
	va_list ap;
	int k;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	k = snprintf(line, sizeof(line), "N2BT %u %llu ", ++n2bt_crumb_seq, (unsigned long long)(ns / 1000000ull));
	va_start(ap, fmt);
	k += vsnprintf(line + k, sizeof(line) - (size_t)k - 2, fmt, ap);
	va_end(ap);
	if (k > (int)sizeof(line) - 2) k = (int)sizeof(line) - 2;
	printf("NORD2 BT0: %s\n", line + 5);
	line[k++] = '\n';
	n2bt_crumb_raw(line, (uint32_t)k);
}

static void
n2bt_crumb_init(void)
{
	volatile uint32_t *h;
	char old[96];
	uint32_t osig, osz, prev_inflight = 0, prev_end = 0;

	if (n2bt_crumb_va != NULL) return;
	printf("NORD2 BT0: mapping the breadcrumb zone %#x (kernel_map, Device)\n", N2BT_CRUMB_PA);
	n2bt_crumb_va = (volatile uint8_t *)ml_io_map_unmappable(N2BT_CRUMB_PA, N2BT_CRUMB_SIZE, VM_WIMG_IO);
	if (n2bt_crumb_va == NULL) {
		printf("NORD2 BT0: breadcrumb zone not mapped: no persistent trace, guard off\n");
		return;
	}
	h = (volatile uint32_t *)(void *)n2bt_crumb_va;
	osig = h[0];
	osz = h[2];                                     /* bt8 zones: 4080 (the whole layout); older boots: what they wrote */
	if (osig == 0x43474244u && osz == N2BT_DATA_END) {
		/* bt11 rule: refuse ONLY when the AP died inside a hardware step. GUARD slot + NOW slot of that boot:
		 *   "N2BT END ..." (bridge started / clean stop / restart returned / alive every 15 s)  -> clean
		 *   "N2BT INFLIGHT"          and NOW = "starting:S2".."starting:S9"                     -> reset in the bring-up: refuse
		 *   "N2BT INFLIGHT restart"  and NOW = rs-bgf / rs-own / rs-func (restart power-on)     -> reset in a restart: refuse
		 *   anything else (NOW = idle/tx/rx/hk-*, S1 waiting, an older zone's leftovers)         -> clean */
		char g[N2BT_GUARD_LEN + 1], nw[N2BT_NOW_LEN + 1];
		for (uint32_t i = 0; i < N2BT_GUARD_LEN; i++) g[i] = (char)n2bt_crumb_va[12u + N2BT_GUARD_AT + i];
		for (uint32_t i = 0; i < N2BT_NOW_LEN; i++) nw[i] = (char)n2bt_crumb_va[12u + N2BT_NOW_AT + i];
		g[N2BT_GUARD_LEN] = 0;
		nw[N2BT_NOW_LEN] = 0;
		prev_end = strncmp(g, "N2BT END", 8) == 0;
		if (strncmp(g, "N2BT INFLIGHT restart", 21) == 0) {
			prev_inflight = n2bt_find(nw, " rs-bgf ") || n2bt_find(nw, " rs-own ") || n2bt_find(nw, " rs-func ");
		} else if (strncmp(g, "N2BT INFLIGHT", 13) == 0) {
			const char *p = n2bt_find(nw, " starting:S");
			prev_inflight = p != NULL && p[11] >= '2' && p[11] <= '9';
		}
		snprintf(old, sizeof(old), "guard \"%.21s\" now \"%.22s\"", g, nw + 9);
	} else if (0 && osig == 0x43474244u && osz <= N2BT_CRUMB_SIZE - 12u) {
		/* bt10: zones of older kernels (linear layout) are no longer judged: they could not tell a run-time hang from a
		 * reset during the bring-up (bt29a was refused after a q10jfinal27 boot). Only the bt8+ GUARD slot decides. */
		/* scan the old text for our two tokens (byte reads from Device memory, aligned by construction) */
		static const char t1[] = "N2BT INFLIGHT", t2[] = "N2BT END";
		uint32_t m1 = 0, m2 = 0;
		for (uint32_t i = 0; i < osz; i++) {
			char c = (char)n2bt_crumb_va[12u + i];
			m1 = (c == t1[m1]) ? m1 + 1 : (c == t1[0] ? 1 : 0);
			m2 = (c == t2[m2]) ? m2 + 1 : (c == t2[0] ? 1 : 0);
			if (m1 == sizeof(t1) - 1) { prev_inflight = 1; m1 = 0; }
			if (m2 == sizeof(t2) - 1) { prev_end = 1; m2 = 0; }
		}
	}
	if (!(osig == 0x43474244u && osz == N2BT_DATA_END)) snprintf(old, sizeof(old), "sig %#x size %u (older layout: not judged)", osig, osz);
	n2bt_prev_reset = prev_inflight && !prev_end;
	h[0] = 0x43474244u; h[1] = 0; h[2] = 0;
	
	for (uint32_t i = 0; i < N2BT_DATA_END; i++) n2bt_crumb_va[12u + i] = (uint8_t)(((i + 1) % 76u) == 0 ? '\n' : ' ');
	n2bt_lin_at = 0;
	n2bt_ring_next = 0;
	h[1] = 0;
	h[2] = N2BT_DATA_END;
	
	n2bt_crumb_raw("====0.0-D\n", 10);
	n2bt_crumb("zone %#x previous boot: %s%s", N2BT_CRUMB_PA, old, n2bt_prev_reset ? " -> THAT BOOT RESET DURING THE BT BRING-UP" : "");
}

