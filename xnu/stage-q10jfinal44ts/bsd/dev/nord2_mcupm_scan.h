#ifndef NORD2_MCUPM_SCAN_H
#define NORD2_MCUPM_SCAN_H
/*
 * Pure helpers of the read-only MCUPM probe n2mcu=2/3 (patch 0114, README-0107-design.md section 11), host-testable: no kernel headers, no register access.
 * They classify blocks of words read from the MCUPM SRAM (all-zero, power-up noise, structure), find printable runs and DRAM-pointer candidates, and diff
 * two reads of the same range.
 */
#include <stdbool.h>
#include <stdint.h>

#define N2MS_BLOCK_WORDS 64u          /* 256-byte blocks */

enum n2ms_class { N2MS_ZERO = 0, N2MS_NOISE = 1, N2MS_STRUCT = 2 };

struct n2ms_block {
	uint32_t zero;                /* words equal to 0 */
	uint32_t ones;                /* set bits */
	uint32_t rep;                 /* words equal to the previous word */
	uint32_t dup;                 /* words equal to some earlier word of the block */
};

static inline void
n2ms_measure(const uint32_t *w, unsigned n, struct n2ms_block *b)
{
	b->zero = b->ones = b->rep = b->dup = 0;
	for (unsigned i = 0; i < n; i++) {
		b->zero += (w[i] == 0);
		b->ones += (uint32_t)__builtin_popcount(w[i]);
		if (i > 0 && w[i] == w[i - 1]) {
			b->rep++;
		}
		for (unsigned k = 0; k < i; k++) {
			if (w[k] == w[i]) {
				b->dup++;
				break;
			}
		}
	}
}

/* All words zero -> ZERO.  Power-up noise: ones within +-5 sigma of n*16 bits (sigma = sqrt(n*8); a 64-word block has ones in [911, 1137]) and no word
 * that repeats an earlier word of the block (one zero word is fine, two are structure).  Everything else (code, tables, partly written memory) is STRUCT. */
static inline enum n2ms_class
n2ms_class_of(const struct n2ms_block *b, unsigned n)
{
	if (b->zero == n) {
		return N2MS_ZERO;
	}
	unsigned bits = n * 32u, sigma_x100 = 0;
	/* sigma * 100 = floor(sqrt(n * 8 * 10000)) (n = 64: 22.62) */
	while ((uint64_t)(sigma_x100 + 1) * (sigma_x100 + 1) <= (uint64_t)n * 8u * 10000u) {
		sigma_x100++;
	}
	uint64_t lo = (uint64_t)bits * 100u / 2u - 5u * (uint64_t)sigma_x100, hi = (uint64_t)bits * 100u / 2u + 5u * (uint64_t)sigma_x100;
	uint64_t o = (uint64_t)b->ones * 100u;
	if (b->dup == 0 && o >= lo && o <= hi) {
		return N2MS_NOISE;
	}
	return N2MS_STRUCT;
}

/* Next printable-ASCII run of at least `minlen` bytes in b[from, n): returns true and sets the offset and the length (the length is not capped). */
static inline bool
n2ms_ascii_next(const uint8_t *b, unsigned n, unsigned from, unsigned minlen, unsigned *off, unsigned *len)
{
	unsigned i = from;
	while (i < n) {
		if (b[i] >= 0x20 && b[i] < 0x7f) {
			unsigned j = i;
			while (j < n && b[j] >= 0x20 && b[j] < 0x7f) {
				j++;
			}
			if (j - i >= minlen) {
				*off = i;
				*len = j - i;
				return true;
			}
			i = j;
		} else {
			i++;
		}
	}
	return false;
}

/* A word that could be a physical address inside the bootloader's MCUPM-reserved mblock of the live FDT (mblock-13-MCUPM-reserved: 0x7e900000, 1 MiB). */
static inline bool
n2ms_ptr_mblock(uint32_t v)
{
	return v >= 0x7e900000u && v < 0x7ea00000u;
}

/* A page-aligned word inside the first DRAM bank of the live FDT (0x40000000..0xc0000000): a candidate for a pointer to a shared buffer (mcupm_share). */
static inline bool
n2ms_ptr_dram_page(uint32_t v)
{
	return v != 0 && (v & 0xfffu) == 0 && v >= 0x40000000u && v < 0xc0000000u;
}

/* Number of words that differ between a[] and b[]; the indices of the first `cap` of them go to idx[]. */
static inline unsigned
n2ms_diff(const uint32_t *a, const uint32_t *b, unsigned n, unsigned *idx, unsigned cap)
{
	unsigned changed = 0;
	for (unsigned i = 0; i < n; i++) {
		if (a[i] != b[i]) {
			if (changed < cap) {
				idx[changed] = i;
			}
			changed++;
		}
	}
	return changed;
}

/* ---- stage-2 ping (patch 0115, README-0107-design.md section 12.6): the only MCUPM test that writes ------------------------------------------------- */
#define N2MS_PING_CMD        0xdeadu          /* the stock alive check (mcupm_alive_show) */
#define N2MS_PING_MBOX0_PA   0x1031fce0u      /* mailbox 0, send half: words 0..2 are written */
#define N2MS_PING_REPLY_OFF  0x50u            /* mailbox 0 receive half: 1 reply word (PIN_R_OFFSET 0x14 slots) */
#define N2MS_PING_SET_OFF    0x154u           /* control page: doorbell register, bit n = mailbox n */
#define N2MS_PING_CLR_OFF    0x74u            /* control page: clears an incoming bit */
#define N2MS_PING_RECV_OFF   0x78u            /* control page: incoming status */
#define N2MS_PING_SEND_OFF   0x150u           /* control page: outgoing still-pending status */
#define N2MS_PING_POLL_US    50u
#define N2MS_PING_POLL_MAX   400u             /* 400 * 50 us = 20 ms */

enum n2ms_ping_out { N2MS_PING_A = 0, N2MS_PING_B, N2MS_PING_C, N2MS_PING_D };

/* Run the ping only from the exact state the probes saw: all four control registers 0 and mailbox 3 slot 8 (TASK_STA) == 1 (INIT). */
static inline bool
n2ms_ping_pre_ok(const uint32_t regs[4], uint32_t task_sta)
{
	return regs[0] == 0 && regs[1] == 0 && regs[2] == 0 && regs[3] == 0 && task_sta == 1;
}

/* A: reply bit set and reply word non-zero (the stock "Alive").  B: reply bit set, word 0.  C: no reply, but the doorbell was seen pending and then
 * consumed, or some other mailbox/window word changed.  D: nothing.  Only A continues. */
static inline enum n2ms_ping_out
n2ms_ping_classify(bool recv_bit, uint32_t reply0, bool send_seen_then_cleared, unsigned changed_words)
{
	if (recv_bit) {
		return reply0 != 0 ? N2MS_PING_A : N2MS_PING_B;
	}
	if (send_seen_then_cleared || changed_words != 0) {
		return N2MS_PING_C;
	}
	return N2MS_PING_D;
}
#endif
