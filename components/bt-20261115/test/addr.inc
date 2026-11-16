static bool
n2bt_addr_ok(const uint8_t a[6])
{
	uint8_t o = 0, x = 0xff;
	for (int i = 0; i < 6; i++) { o |= a[i]; x &= a[i]; }
	return o != 0 && x != 0xff;
}

static bool
n2bt_addr_publishable(const uint8_t a[6])
{
	return n2bt_addr_ok(a) && !(a[0] == 0x00 && a[1] == 0x00 && a[2] == 0x46) && !(a[3] == 0x9e && a[4] == 0x8b && a[5] < 0x40);
}

static int
n2bt_parse_addr(const char *t, size_t n, uint8_t a[6])
{
	size_t len = 0;
	while (len < n && t[len] != 0) len++;
	while (len > 0 && (t[len - 1] == '\n' || t[len - 1] == '\r' || t[len - 1] == ' ')) len--;
	if (len != 17) return EINVAL;
	for (int i = 0; i < 6; i++) {
		unsigned v = 0;
		for (int k = 0; k < 2; k++) {
			char c = t[3 * i + k];
			unsigned d = (c >= '0' && c <= '9') ? (unsigned)(c - '0') : (c >= 'a' && c <= 'f') ? (unsigned)(c - 'a' + 10) :
			    (c >= 'A' && c <= 'F') ? (unsigned)(c - 'A' + 10) : 99u;
			if (d > 15) return EINVAL;
			v = v << 4 | d;
		}
		if (i < 5 && t[3 * i + 2] != ':') return EINVAL;
		a[i] = (uint8_t)v;
	}
	return n2bt_addr_publishable(a) ? 0 : EINVAL;
}

static void
n2bt_make_addr(uint8_t a[6])
{
	do {
		read_random(a, 6);
		a[0] &= 0xfc;
	} while (!n2bt_addr_publishable(a));
}

int
n2bt_addr_cmd(uint8_t c[10])
{
	if (!n2bt_have_req_addr) return 0;
	c[0] = 0x01; c[1] = 0x1a; c[2] = 0xfc; c[3] = 0x06;
	for (int i = 0; i < 6; i++) c[4 + i] = n2bt_req_addr[5 - i];
	return 10;
}

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

