/* Open-drain I2C8 bus model for the Nord2 audio-ID host tests.
 *
 * Wire-level: SCL/SDA are high unless the master or any slave pulls low.
 * Slaves react to edges like real devices (sample on SCL rise, change SDA on
 * SCL fall). Bystanders at 0x2f/0x35/0x42/0x51/0x56 model the other I2C8
 * devices (wl2868c, fan53870, fsa4480, ext_buck_vmddr, ext_buck_vufs12): they ACK writes and
 * count any data byte, so a test can prove that none was ever addressed.
 * A monitor decodes every byte on the wire independently of any slave. */
#ifndef NORD2_AUDIO_I2C_SIM_H
#define NORD2_AUDIO_I2C_SIM_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SIM_DEVS 7
#define SIM_LOG 8192

enum { SD_IDLE, SD_RX_ADDR, SD_RX_REG, SD_RX_DATA, SD_TX, SD_IGNORE };

struct sim_dev {
	uint8_t addr;
	int present;
	uint16_t reg[256];
	int nack_reg[256];
	int stuck_scl_after_addr;  /* hold SCL low forever after the address ACK */
	unsigned stretch_us;       /* one stretch before the first TX data bit */
	int stretch_armed;
	int stuck_sda;             /* pull SDA low permanently from the start */
	/* protocol state */
	int state, bit, ack_phase, rw, master_ack, sent, tx_index;
	uint8_t shift, ptr, tx_byte;
	uint16_t tx_word;
	int sda_low, scl_low;
	uint64_t scl_release_at;
	unsigned addressed, ptr_writes, data_writes;
};

struct sim_rec {
	uint8_t value;
	int is_addr, rw, ack, frame;
};

struct sim {
	int m_scl_low, m_sda_low;
	int scl, sda;
	uint64_t now_us;
	struct sim_dev dev[SIM_DEVS];
	/* monitor */
	int mon_active, mon_bit, mon_pos, mon_rw, mon_frame;
	uint8_t mon_shift;
	int mon_ack;
	struct sim_rec log[SIM_LOG];
	unsigned nlog, starts, stops, rises;
	unsigned master_drive_high; /* must stay 0: open drain only */
};

static void
sim_fail(const char *what)
{
	fprintf(stderr, "SIM FAIL: %s\n", what);
	abort();
}

static void
sim_dev_init(struct sim_dev *d, uint8_t addr, int present)
{
	memset(d, 0, sizeof(*d));
	d->addr = addr;
	d->present = present;
}

static void
sim_init(struct sim *s)
{
	memset(s, 0, sizeof(*s));
	s->scl = s->sda = 1;
	/* 0: TFA @0x34, 1: TFA @0x37, then shared-bus bystanders. */
	sim_dev_init(&s->dev[0], 0x34, 1);
	sim_dev_init(&s->dev[1], 0x37, 1);
	sim_dev_init(&s->dev[2], 0x35, 1);
	sim_dev_init(&s->dev[3], 0x51, 1);
	sim_dev_init(&s->dev[4], 0x56, 1);
	sim_dev_init(&s->dev[5], 0x2f, 1);
	sim_dev_init(&s->dev[6], 0x42, 1); /* fsa4480 headset switch */
}

static void
sim_tx_bit(struct sim_dev *d)
{
	d->sda_low = !((d->tx_byte >> (7 - d->sent)) & 1);
}

static void
sim_dev_start(struct sim_dev *d)
{
	if (d->stuck_sda) { return; }
	d->state = SD_RX_ADDR;
	d->bit = 0;
	d->shift = 0;
	d->ack_phase = 0;
	d->sda_low = 0;
}

static void
sim_dev_stop(struct sim_dev *d)
{
	if (d->stuck_sda) { return; }
	d->state = SD_IDLE;
	d->sda_low = 0;
}

static void
sim_dev_rise(struct sim_dev *d, int sda)
{
	if (d->state == SD_IDLE || d->state == SD_IGNORE) { return; }
	if (d->state == SD_TX) {
		if (d->ack_phase) { d->master_ack = !sda; }
		return;
	}
	if (!d->ack_phase && d->bit < 8) {
		d->shift = (uint8_t)((d->shift << 1) | (sda ? 1 : 0));
		d->bit++;
	}
}

static void
sim_dev_fall(struct sim *s, struct sim_dev *d)
{
	if (d->state == SD_IDLE || d->state == SD_IGNORE) { return; }
	if (d->state == SD_TX) {
		if (!d->ack_phase) {
			if (d->stretch_armed && d->sent == 0 && d->tx_index == 0) {
				d->stretch_armed = 0;
				d->scl_low = 1;
				d->scl_release_at = s->now_us + d->stretch_us;
			}
			d->sent++;
			if (d->sent < 8) { sim_tx_bit(d); }
			else { d->sda_low = 0; d->ack_phase = 1; }
			return;
		}
		d->ack_phase = 0;
		if (!d->master_ack) { d->state = SD_IGNORE; d->sda_low = 0; return; }
		d->tx_index++;
		d->tx_byte = d->tx_index == 1 ? (uint8_t)(d->tx_word & 0xff) : 0xff;
		d->sent = 0;
		sim_tx_bit(d);
		return;
	}
	if (!d->ack_phase) {
		if (d->bit < 8) { return; }
		int ack = 0;
		if (d->state == SD_RX_ADDR) {
			if ((d->shift >> 1) == d->addr && d->present) {
				ack = 1;
				d->addressed++;
				d->rw = d->shift & 1;
			}
		} else if (d->state == SD_RX_REG) {
			d->ptr = d->shift;
			d->ptr_writes++;
			ack = !d->nack_reg[d->ptr];
		} else if (d->state == SD_RX_DATA) {
			d->data_writes++;
			ack = 1;
		}
		if (!ack) { d->state = SD_IGNORE; return; }
		d->sda_low = 1;
		d->ack_phase = 1;
		return;
	}
	/* Falling edge after the ninth clock of a received byte. */
	d->ack_phase = 0;
	d->sda_low = 0;
	d->bit = 0;
	d->shift = 0;
	if (d->state == SD_RX_ADDR) {
		if (d->stuck_scl_after_addr) { d->scl_low = 1; d->scl_release_at = UINT64_MAX; }
		if (d->rw) {
			d->state = SD_TX;
			d->tx_word = d->reg[d->ptr];
			d->tx_index = 0;
			d->tx_byte = (uint8_t)(d->tx_word >> 8);
			d->sent = 0;
			sim_tx_bit(d);
		} else {
			d->state = SD_RX_REG;
		}
	} else if (d->state == SD_RX_REG || d->state == SD_RX_DATA) {
		d->state = SD_RX_DATA;
	}
}

static void
sim_mon_rise(struct sim *s)
{
	if (!s->mon_active) { return; }
	if (s->mon_bit < 8) {
		s->mon_shift = (uint8_t)((s->mon_shift << 1) | (s->sda ? 1 : 0));
		s->mon_bit++;
	} else if (s->mon_bit == 8) {
		s->mon_ack = !s->sda;
		s->mon_bit = 9;
	}
}

static void
sim_mon_fall(struct sim *s)
{
	if (!s->mon_active || s->mon_bit != 9) { return; }
	if (s->nlog >= SIM_LOG) { sim_fail("log overflow"); }
	struct sim_rec *r = &s->log[s->nlog++];
	r->value = s->mon_shift;
	r->is_addr = s->mon_pos == 0;
	if (r->is_addr) { s->mon_rw = s->mon_shift & 1; }
	r->rw = s->mon_rw;
	r->ack = s->mon_ack;
	r->frame = s->mon_frame;
	s->mon_pos++;
	s->mon_bit = 0;
	s->mon_shift = 0;
}

static void
sim_update(struct sim *s)
{
	for (unsigned iter = 0; iter < 16; iter++) {
		int slave_scl = 0, slave_sda = 0;
		for (unsigned i = 0; i < SIM_DEVS; i++) {
			slave_scl |= s->dev[i].scl_low;
			slave_sda |= s->dev[i].sda_low || s->dev[i].stuck_sda;
		}
		int scl = !(s->m_scl_low || slave_scl);
		int sda = !(s->m_sda_low || slave_sda);
		if (scl == s->scl && sda == s->sda) { return; }
		if (scl != s->scl) {
			s->scl = scl;
			if (scl) {
				s->rises++;
				sim_mon_rise(s);
				for (unsigned i = 0; i < SIM_DEVS; i++) { sim_dev_rise(&s->dev[i], s->sda); }
			} else {
				sim_mon_fall(s);
				for (unsigned i = 0; i < SIM_DEVS; i++) { sim_dev_fall(s, &s->dev[i]); }
			}
			continue; /* re-resolve SDA after slaves react to the clock */
		}
		int fell = s->sda && !sda;
		s->sda = sda;
		if (s->scl && fell) {
			s->starts++;
			s->mon_active = 1;
			s->mon_bit = 0;
			s->mon_pos = 0;
			s->mon_shift = 0;
			s->mon_frame++;
			for (unsigned i = 0; i < SIM_DEVS; i++) { sim_dev_start(&s->dev[i]); }
		} else if (s->scl && !fell) {
			s->stops++;
			s->mon_active = 0;
			for (unsigned i = 0; i < SIM_DEVS; i++) { sim_dev_stop(&s->dev[i]); }
		}
	}
	sim_fail("bus did not settle");
}

static void
sim_advance(struct sim *s, unsigned us)
{
	s->now_us += us;
	for (unsigned i = 0; i < SIM_DEVS; i++) {
		struct sim_dev *d = &s->dev[i];
		if (d->scl_low && d->scl_release_at != UINT64_MAX && s->now_us >= d->scl_release_at) {
			d->scl_low = 0;
		}
	}
	sim_update(s);
}

static void
sim_master(struct sim *s, int scl_low, int sda_low)
{
	s->m_scl_low = scl_low;
	s->m_sda_low = sda_low;
	sim_update(s);
}

/* Every address byte targets 0x34/0x37; each write frame carries at most one
 * byte (the register pointer) from the allowlist; bystanders never addressed. */
static void
sim_check_wire(const struct sim *s, const uint8_t *regs, unsigned nregs)
{
	int frame = -1, writes = 0;
	for (unsigned i = 0; i < s->nlog; i++) {
		const struct sim_rec *r = &s->log[i];
		if (r->frame != frame) { frame = r->frame; writes = 0; }
		if (r->is_addr) {
			if ((r->value >> 1) != 0x34 && (r->value >> 1) != 0x37) { sim_fail("foreign address byte"); }
			continue;
		}
		if (r->rw) { continue; }
		if (++writes > 1) { sim_fail("data byte written after register pointer"); }
		int ok = 0;
		for (unsigned j = 0; j < nregs; j++) { ok |= r->value == regs[j]; }
		if (!ok) { sim_fail("register pointer outside allowlist"); }
	}
	for (unsigned i = 0; i < SIM_DEVS; i++) {
		if (s->dev[i].data_writes) { sim_fail("a device received a data byte"); }
		if (i >= 2 && s->dev[i].addressed) { sim_fail("shared-bus bystander addressed"); }
	}
	if (s->master_drive_high) { sim_fail("master drove a line high"); }
}
#endif
