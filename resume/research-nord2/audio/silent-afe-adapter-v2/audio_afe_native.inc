/* PRIVATE future bridge only. Include after audio_id_bind.v2.inc in a copied
 * sweet_touch.c for compile review. There is deliberately NO call site.
 * Revision5's SRAM slice is 0x11212000..0x11212fff, one 4 KiB block of
 * mt6893.dts:8059-8065. XNU's 16 KiB AFE mapping already covers it; do
 * not create another VA mapping of the same physical page. */
#include "audio_afe_adapter.h"

static struct {
	struct naa_state state;
	thread_t worker;
	unsigned attempted;
} naa_native;

static int
naa_native_worker(void *cookie, uintptr_t *token)
{
	(void)cookie;
	if (!token || !naa_native.worker || current_thread() != naa_native.worker) {
		return -1;
	}
	*token = (uintptr_t)current_thread();
	return 0;
}

static vm_offset_t
naa_native_va(uint32_t pa)
{
	if (pa >= NAF_SRAM && pa < NAF_SRAM + NAF_SRAM_BYTES) {
		return nab.afe && PAGE_SIZE == 0x4000u ?
		    nab.afe + (pa - NAF_AFE) : 0;
	}
	/* nab_va only admits its one-page mappings; no indirect I2C8 access. */
	if ((pa & ~0xfffu) == 0x11d04000u ||
	    (pa & ~0xfffu) == 0x11000000u ||
	    (pa & ~0xfffu) == 0x11d05000u ||
	    (pa & ~0xfffu) == 0x11d10000u ||
	    (pa & ~0xfffu) == 0x11c20000u) { return 0; }
	return nab_va(pa, 4);
}

static int
naa_native_read32(void *cookie, uint32_t pa, uint32_t *v)
{
	(void)cookie;
	if (!v || current_thread() != naa_native.worker || (pa & 3u) ||
	    (pa >= NAF_SRAM && pa < NAF_SRAM + NAF_SRAM_BYTES)) { return -1; }
	vm_offset_t va = naa_native_va(pa);
	if (!va) { return -1; }
	*v = nab_mmio_read(va, 4);
	return 0;
}

static int
naa_native_write32(void *cookie, uint32_t pa, uint32_t v)
{
	(void)cookie;
	if (current_thread() != naa_native.worker || !naf_write_allowed(pa)) { return -1; }
	vm_offset_t va = naa_native_va(pa);
	if (!va) { return -1; }
	nab_mmio_write(va, 4, v);
	return 0;
}

static int
naa_native_delay(void *cookie, unsigned us)
{
	(void)cookie;
	if (current_thread() != naa_native.worker || us > 1000u) { return -1; }
	delay(us);
	return 0;
}

static const struct naa_backend naa_native_backend = {
	naa_native_worker, naa_native_read32, naa_native_write32, naa_native_delay
};

/* A future, separately authorized caller must establish exclusive SRAM and
 * audio-master ownership before calling this. Reusing the existing VA is
 * not hardware ownership proof. This function is intentionally uncalled. */
static __attribute__((used)) int
naa_native_prepare(void)
{
	if (naa_native.attempted || nab.level != 2 || nab.status || !nab.probed ||
	    nab.probe_rc || !na8_return_ok(&nab.bus) || nab.afe_gate ||
	    !nab.evidence[0].measured || !nab.evidence[1].measured ||
	    !nab.afe_measure_valid || !nab.afe_measure_worker ||
	    current_thread() != nab.afe_measure_worker || PAGE_SIZE != 0x4000u ||
	    !nab.spm || !nab.infra || !nab.top || !nab.apmixed || !nab.afe ||
	    !ni_gpio || !ni_lt || !current_thread()) { return -1; }
	naa_native.attempted = 1;
	nab.afe_measure_valid = 0; /* exactly one consume on measured worker */
	naa_native.worker = nab.afe_measure_worker;
	return naa_prepare(&naa_native.state, &naa_native_backend, 0,
	    (uintptr_t)naa_native.worker, &nab.state, nab.evidence);
}
