/*
 * SWEET: USB bring-up, step 1 - read-only state probe.
 *
 * The phone's USB is a Synopsys DWC3 (USB 2.0 only, maximum-speed
 * high-speed) at 0x0a600000 behind Qualcomm's dwc-usb3-msm wrapper, with a
 * QUSB2 PHY at 0x088e2000; its power domain is the USB30_PRIM GDSC and its
 * clocks come from GCC (offsets from Linux gcc-sdmmagpie.c). ABL uses this
 * controller for fastboot; this probe only reports what state it is left
 * in after `fastboot boot`. It never writes a register, and it touches the
 * DWC3 / PHY register blocks only when the GCC status registers say their
 * power domain and clocks are on (an access to a gated block would hang).
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <sys/param.h>
#include <sys/systm.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <kern/cpu_number.h>
#include <kern/processor.h>
#include <kern/sched_prim.h>
#include <machine/machine_routines.h>
#include <arm/caches_internal.h>
#include <vm/pmap.h>
#include <vm/vm_kern.h>
#include <pexpert/pexpert.h>
#include <kern/locks.h>
#include <sys/kpi_mbuf.h>
#include <sys/kpi_socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/ethernet.h>
#include <net/if_ether.h>
#include <net/ether_if_module.h>
#include <net/kpi_interface.h>
#include <net/kpi_protocol.h>
#include <netinet/in.h>
#include <net/if_media.h>
#include <net/dlil.h>
#include <sys/kern_event.h>

#include <arm64/sweet_early.h>

#if defined(NORD2)
#include "wifi_live_control.h"
#include "audio_live.h"
extern int nat_usb(const uint8_t setup[8], uint8_t *, size_t, uint32_t *length);   /* 0296 */
extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
extern kern_return_t thread_terminate(thread_t);
/* sched_prim.h exposes this only to the osfmk compilation unit. */
extern processor_t thread_bind(processor_t);
/*
 * MT6893 MTU3 bring-up. Register layout: the device's Linux mtu3_hw_regs.h;
 * clock gates: clk-mt6885.c (also used by MT6893). Live DT confirms MAC
 * 0x11201000, IPPC 0x11203e00, SPI96 and high-speed maximum. Point13 only
 * reads state; it never enables clocks, clears status, touches FIFO, starts
 * DMA or changes the USB connection. Check clock/power gates before MAC.
 */
static uint32_t
nord2_usb_read(vm_offset_t base, uint32_t offset)
{
	uint32_t value = *(volatile uint32_t *)(base + offset);
	__asm__ volatile("dmb oshld" ::: "memory");
	return value;
}

static void nord2_usb_enumerate(vm_offset_t mac, bool prepare_phy, bool background);
static void nusb_background_thread(void *, wait_result_t);
static unsigned nusb_background_state;
static unsigned nusb_background_stop;
static bool nusb_smp;
static int nusb_sleep_chan;

static void
nusb_sleep_ms(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&nusb_sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

/*
 * 0700 (usbchg-20261115): boot WITHOUT a USB host.
 * Before 0700, mode 23 (the normal iOS boot) treated "no usable USB" as a diagnostic stop point: when the phone is powered on
 * without a cable the preloader/LK never powers the SSUSB block, INFRA_CG3 keeps the USB clocks gated, nord2_usb_probe() went
 * to `done` and nord2_stop_at(23) left XNU (stub chain-back to LK fastboot, the RTC AUTOFB bit) = the "kernel hangs" report.
 * Now mode 23 logs the reason and keeps booting without the console; a late thread re-checks the USB state.
 *   kern.n2usb_strict  1 = the old behaviour (any USB unavailability in mode 23 leaves XNU), default 0
 *   kern.n2usb_late    0 = no late thread work, 1 = passive (read-only re-check of the clock/IP gates every 2 s, start the console
 *                      when LK-equivalent state appears), 2 = also power the USB block up ourselves once VBUS is seen (clock
 *                      ungating + IPPC device power-on, Linux mtu3 order; EXPERIMENTAL), default 1
 *   kern.n2usb_state   (read) 0 not run, 1 console up at boot, 2 no USB at boot (booted on), 3 late console up, 4 refused, 5 stopped
 *   kern.n2usb_reason  (read) why the console was unavailable (N2USB_WHY_*)
 * Log: the NORD2 USB14 lines below.
 */
#include <sys/sysctl.h>
enum { N2USB_WHY_NONE = 0, N2USB_WHY_SMP = 1, N2USB_WHY_MAP = 2, N2USB_WHY_CLOCK = 3, N2USB_WHY_INACTIVE = 4, N2USB_WHY_THREAD = 5,
       N2USB_WHY_TIMEOUT = 6, N2USB_WHY_CPUS = 7, N2USB_WHY_REFUSED = 8 };
static uint32_t n2usb_strict;
static uint32_t n2usb_late = 1;
static uint32_t n2usb_state;
static uint32_t n2usb_reason;
static uint32_t n2usb_late_checks, n2usb_powerups;
/* written by the battery/charger poll (0701): 1 = MP2650 reports VIN power good, 0 = no VBUS, 2 = unknown (no sample yet) */
extern volatile uint32_t nord2_chg_vbus_good;
volatile uint32_t nord2_chg_vbus_good = 2;
SYSCTL_UINT(_kern, OID_AUTO, n2usb_strict, CTLFLAG_RW | CTLFLAG_LOCKED, &n2usb_strict, 0,
    "0700: 1 = mode 23 leaves XNU when USB is unavailable (old behaviour)");
SYSCTL_UINT(_kern, OID_AUTO, n2usb_late, CTLFLAG_RW | CTLFLAG_LOCKED, &n2usb_late, 0,
    "0700: 0 off, 1 passive USB re-check every 2 s, 2 + power the USB block up when VBUS is seen (experimental)");
SYSCTL_UINT(_kern, OID_AUTO, n2usb_state, CTLFLAG_RD | CTLFLAG_LOCKED, &n2usb_state, 0, "0700: USB console state");
SYSCTL_UINT(_kern, OID_AUTO, n2usb_reason, CTLFLAG_RD | CTLFLAG_LOCKED, &n2usb_reason, 0, "0700: why USB was unavailable");
static void n2usb_no_host_boot(unsigned why);

void
nord2_usb_probe(void)
{
	int stop = 0;
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (stop != 13 && stop != 14 && stop != 15 && stop != 16 && stop != 21 && stop != 22 && stop != 23) {
		return;
	}
	if ((stop == 21 || stop == 22 || stop == 23) && !nord2_smp_userland_ready()) {
		printf("NORD2 USB: all8 SMP pass required before USB/userland\n");
		n2usb_reason = N2USB_WHY_SMP;
		goto done;
	}
	if (stop == 13) {
		printf("NORD2 USB: MTU3 snapshot begin (read-only)\n");
	} else {
		printf("NORD2 USB: MTU3 probe begin, point%d\n", stop);
	}
	vm_offset_t infra = ml_io_map(0x10001000, 0x1000);
	vm_offset_t top = ml_io_map(0x10000000, 0x1000);
	if (!infra || !top) {
		printf("NORD2 USB: clock mapping failed\n");
		n2usb_reason = N2USB_WHY_MAP;
		goto done;
	}
	uint32_t cg = nord2_usb_read(infra, 0xac);
	uint32_t cfg9 = nord2_usb_read(top, 0xa0);
	uint32_t cfg10 = nord2_usb_read(top, 0xb0);
	printf("NORD2 USB: INFRA_CG3=%08x TOP_CFG9=%08x TOP_CFG10=%08x\n",
	    cg, cfg9, cfg10);
	if ((cg & ((1u << 1) | (1u << 31))) || (cfg9 & (1u << 31)) ||
	    (cfg10 & (1u << 7))) {
		printf("NORD2 USB: clock gated; IPPC/MAC not read\n");
		n2usb_reason = N2USB_WHY_CLOCK;
		goto done;
	}
	vm_offset_t ippc = ml_io_map(0x11203e00, 0x100);
	if (!ippc) {
		printf("NORD2 USB: IPPC mapping failed\n");
		n2usb_reason = N2USB_WHY_MAP;
		goto done;
	}
	const uint16_t ipregs[] = {0, 4, 8, 0x10, 0x14, 0x18, 0x28,
	    0x30, 0x50, 0x8c, 0x98, 0xa0, 0xa4};
	for (unsigned i = 0; stop == 13 && i < sizeof(ipregs) / sizeof(ipregs[0]); i++) {
		printf("NORD2 USB: IPPC[%03x]=%08x\n", ipregs[i],
		    nord2_usb_read(ippc, ipregs[i]));
	}
	uint32_t power = nord2_usb_read(ippc, 8);
	uint32_t status = nord2_usb_read(ippc, 0x14);
	uint32_t port = nord2_usb_read(ippc, 0x50);
	if ((power & 1) || !(status & 1) || (port & 7)) {
		printf("NORD2 USB: device inactive; MAC not read\n");
		n2usb_reason = N2USB_WHY_INACTIVE;
		goto done;
	}
	vm_offset_t mac = ml_io_map(0x11201000, 0x2e00);
	if (!mac) {
		printf("NORD2 USB: MAC mapping failed\n");
		n2usb_reason = N2USB_WHY_MAP;
		goto done;
	}
	if (stop == 14 || stop == 15) {
		nord2_usb_enumerate(mac, stop == 15, false);
		goto done;
	}
	if (stop == 16 || stop == 21 || stop == 22 || stop == 23) {
		/* The SMP service is pinned to CPU0. A return from another core
		 * routes through CPU0, which masks IRQs before MAC/PHY cleanup. */
		int cpus = 0;
		PE_parse_boot_argn("cpus", &cpus, sizeof(cpus));
		if ((stop == 16 && cpus != 1) || ((stop == 21 || stop == 22 || stop == 23) && cpus != 8)) {
			printf("NORD2 USB: invalid background CPU configuration\n");
			n2usb_reason = N2USB_WHY_CPUS;
			goto done;
		}
		thread_t thread;
		nusb_smp = stop == 21 || stop == 22 || stop == 23;
		nusb_background_stop = (unsigned)stop;
		__atomic_store_n(&nusb_background_state, 0, __ATOMIC_RELEASE);
		if (kernel_thread_start(nusb_background_thread, (void *)mac, &thread) != KERN_SUCCESS) {
			printf("NORD2 USB: background thread creation failed\n");
			n2usb_reason = N2USB_WHY_THREAD;
			goto done;
		}
		thread_deallocate(thread);
		/* Yield so initialization completes before the RAM-root diagnostic. */
		for (unsigned i = 0;
		    !__atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE) && i < 10000; i++) {
			nusb_sleep_ms(1);
		}
		if (__atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE) == 1) {
			printf("NORD2 USB: background console ready; continuing BSD boot\n");
			if (stop == 23) { n2usb_state = 1; printf("NORD2 USB14: console up at boot (host cable present)\n"); }
			return;  /* Continue root selection while the bounded USB service runs. */
		}
		printf("NORD2 USB: background initialization failed or timed out\n");
		n2usb_reason = __atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE) == 2 ? N2USB_WHY_REFUSED : N2USB_WHY_TIMEOUT;
		goto done;
	}
	/* Status registers are W1C, not read-to-clear. Never read the FIFOs. */
	const uint16_t regs[] = {0, 4, 0x80, 0x84, 0x100, 0x108,
	    0x110, 0x114, 0x118, 0x210, 0x214, 0x218,
	    0x400, 0x510, 0x514, 0x518, 0x610, 0x614, 0x618,
	    0x700, 0x704, 0x800, 0x850, 0x854, 0xc04, 0xc08,
	    0xc0c, 0xc10, 0xc84, 0x141c, 0x2404, 0x240c,
	    0x2418, 0x241c, 0x243c, 0x2460};
	for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		printf("NORD2 USB: MAC[%04x]=%08x\n", regs[i],
		    nord2_usb_read(mac, regs[i]));
	}
 done:
	printf("NORD2 USB: MTU3 snapshot end\n");
	/* 0700: in the normal iOS mode a missing/unusable USB is not a stop point (SMP failure still is). */
	if (stop == 23 && !n2usb_strict && n2usb_reason != N2USB_WHY_SMP) {
		n2usb_no_host_boot(n2usb_reason);
		return;
	}
	nord2_stop_at((unsigned)stop, stop == 13 ? "after read-only MTU3 snapshot" :
	    stop == 14 ? "after bounded MTU3 USB console" :
	    stop == 15 ? "after bounded MTU3 PHY USB console" :
	    "after background MTU3 USB console");
}
/* NORD2 USB CONTROL: bounded EP0 PIO transport, no DMA or data endpoints. */
#define NUSB_CSR_W1C ((1u << 16) | (1u << 17) | (1u << 22))
#define NUSB_SETUP   (1u << 17)
#define NUSB_TX      (1u << 18)
#define NUSB_END     (1u << 19)
#define NUSB_DIR_IN  (1u << 20)
#define NUSB_STALL   (1u << 25)
#define NUSB_SOFTCON (1u << 6)

static struct {
	vm_offset_t mac;
	bool owned;
	uint32_t power, lv1ie, epie, commonie, linkie, devconf;
	uint32_t requests, resets, log_reads, stalls;
	uint8_t configuration;
} nusb;

/* TPHY v2 COM offsets, from this device's phy-mtk-tphy.c and live DT. */
static const uint16_t nusb_phy_offsets[] = {0, 0x14, 0x18, 0x1c, 0x20, 0x68, 0x6c};
static struct {
	vm_offset_t com;
	bool owned;
	uint32_t saved[7];
} nusb_phy;

/*
 * Local diagnostic uses the recovery's VID/PID, with its own udev access
 * rule matching product/serial. The vendor interface is NOT ADB:
 * subclass 0x53/protocol1 and product "Nord2 XNU console". Local test only.
 */
static const uint8_t nusb_device[] = {
	18, 1, 0, 2, 0, 0, 0, 64, 0xd1, 0x18, 1, 0xd0, 0, 1, 1, 2, 3, 1
};
static const uint8_t nusb_config[] = {
	9, 2, 18, 0, 1, 1, 0, 0x80, 250,
	9, 4, 0, 0, 0, 0xff, 0x53, 1, 0
};
static const uint8_t nusb_qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
static const char *const nusb_strings[] = {
	NULL, "ios-sweet", "Nord2 XNU console", "nord2-xnu"
};

static uint16_t
nusb_le16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}

static uint32_t
nusb_string(unsigned index, uint8_t *out)
{
	if (index == 0) {
		out[0] = 4; out[1] = 3; out[2] = 9; out[3] = 4;
		return 4;
	}
	if (index >= sizeof(nusb_strings) / sizeof(nusb_strings[0])) {
		return 0;
	}
	uint32_t n = 2;
	for (const char *s = nusb_strings[index]; *s && n < 126; s++) {
		out[n++] = (uint8_t)*s;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n; out[1] = 3;
	return n;
}

/*
 * Pure request decoder: 0=STALL, 1=IN data, 2=no-data ACK.
 * Descriptors/configuration, bounded log/status reads and optional fixed
 * Wi-Fi owner mailbox. No data-stage OUT, memory/disk access or code execution.
 */
static int
nusb_reply(const uint8_t setup[8], uint8_t data[1024], uint32_t *length,
    int *address, int *configuration)
{
	uint8_t type = setup[0], request = setup[1];
	uint16_t value = nusb_le16(setup + 2), index = nusb_le16(setup + 4);
	uint16_t wanted = nusb_le16(setup + 6);
	uint32_t n = 0;
	*address = *configuration = -1;
	*length = 0;
	int live_response = nwl_usb(setup, data, 1024, length);
	if (live_response == -1) live_response = nat_usb(setup, data, 1024, length);   /* 0296: nord2_autotest.inc, asks nal_usb first */
	if (live_response >= 0) { return live_response; }
	if (type == 0xc0 && request == 0x51 && index == 0) {
		n = nord2_log_read(value, data, MIN((uint32_t)wanted, 1024u));
		nusb.log_reads++;
	} else if (type == 0xc0 && request == 0x52 && value == 0 && index == 0) {
		const uint32_t status[] = {0x4253554e, 2, nusb.requests,
		    nusb.resets, nusb.log_reads, nusb.stalls, nusb.configuration};
		memcpy(data, status, sizeof(status));
		n = sizeof(status);
	} else if (type == 0xc0 && request == 0x53 && value == 0 && index == 0) {
		uint32_t info[2];
		nord2_log_stream_info(info);
		memcpy(data, info, sizeof(info));
		n = sizeof(info);
	} else if (type == 0xc0 && request == 0x54) {
		n = nord2_log_stream_read((uint32_t)value | (uint32_t)index << 16,
		    data, MIN((uint32_t)wanted, 1024u));
		nusb.log_reads++;
	} else if (type == 0x80 && request == 6) {
		unsigned kind = value >> 8, number = value & 0xff;
		if (kind != 3 && (index || number)) {
			return 0;
		}
		switch (kind) {
		case 1: n = sizeof(nusb_device); memcpy(data, nusb_device, n); break;
		case 2: n = sizeof(nusb_config); memcpy(data, nusb_config, n); break;
		case 3:
			if (index != 0 && index != 0x409) { return 0; }
			n = nusb_string(number, data);
			if (!n) { return 0; }
			break;
		case 6: n = sizeof(nusb_qualifier); memcpy(data, nusb_qualifier, n); break;
		case 7: n = sizeof(nusb_config); memcpy(data, nusb_config, n); data[1] = 7; break;
		default: return 0;
		}
	} else if (type == 0 && request == 5 && value <= 127 && !index && !wanted) {
		*address = value; return 2;
	} else if (type == 0 && request == 9 && value <= 1 && !index && !wanted) {
		*configuration = value; return 2;
	} else if (type == 0x80 && request == 8 && !value && !index && wanted == 1) {
		data[0] = nusb.configuration; n = 1;
	} else if ((type == 0x80 || type == 0x81 || type == 0x82) && request == 0 &&
	    !value && wanted == 2 &&
	    (index == 0 || (type == 0x82 && index == 0x80))) {
		data[0] = data[1] = 0; n = 2;
	} else if (type == 0x81 && request == 10 && !value && !index && wanted == 1 &&
	    nusb.configuration == 1) {
		data[0] = 0; n = 1;
	} else if (type == 1 && request == 11 && !value && !index && !wanted &&
	    nusb.configuration == 1) {
		return 2;
	} else {
		return 0;
	}
	*length = MIN(n, (uint32_t)wanted);
	return wanted ? 1 : 2;
}

static uint32_t
nusb_rd(uint32_t offset)
{
	return nord2_usb_read(nusb.mac, offset);
}

static void
nusb_wr(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(nusb.mac + offset) = value;
	__asm__ volatile("dmb oshst" ::: "memory");
}

static void
nusb_fifo_write(const uint8_t *data, uint32_t count)
{
	uint32_t i = 0;
	for (; i + 4 <= count; i += 4) {
		uint32_t word;
		memcpy(&word, data + i, 4);
		nusb_wr(0x300, word);
	}
	if (count - i >= 2) {
		*(volatile uint16_t *)(nusb.mac + 0x300) = nusb_le16(data + i);
		i += 2;
	}
	if (i < count) {
		*(volatile uint8_t *)(nusb.mac + 0x300) = data[i];
	}
	__asm__ volatile("dmb oshst" ::: "memory");
}

static uint32_t
nusb_phy_rd(uint32_t offset)
{
	return nord2_usb_read(nusb_phy.com, offset);
}

static void
nusb_phy_wr(uint32_t offset, uint32_t value)
{
	*(volatile uint32_t *)(nusb_phy.com + offset) = value;
	__asm__ volatile("dmb oshst" ::: "memory");
}

static bool
nusb_phy_snapshot(void)
{
	/* Called only after the existing clock and active-device MAC guards. */
	vm_offset_t base = ml_io_map(0x11e40000, 0x700);
	if (!base) { return false; }
	nusb_phy.com = base + 0x300;  /* generic-tphy-v2, not the v1 layout */
	for (unsigned i = 0; i < 7; i++) {
		nusb_phy.saved[i] = nusb_phy_rd(nusb_phy_offsets[i]);
		printf("NORD2 USB: PHY[%03x]=%08x\n",
		    0x300 + nusb_phy_offsets[i], nusb_phy.saved[i]);
		if (nusb_phy.saved[i] == ~0u) { return false; }
	}
	return true;
}

/*
 * Restore MAC control of the USB2 pins, following MT6893's TPHY power-on
 * and device-mode path. Keep analog calibration, clocks, supplies, the
 * charger IC and Type-C routing unchanged. All seven modified registers
 * are saved before the first write and restored after MAC disconnect.
 */
static void
nusb_phy_enable(void)
{
	nusb_phy.owned = true;
	/* Leave UART mode; power-on suspend pulse used by MT6893 Linux. */
	uint32_t dtm0 = nusb_phy_rd(0x68) & ~0xc4000000u;
	nusb_phy_wr(0x68, dtm0 | (1u << 18));
	nusb_phy_wr(0x68, (dtm0 | (1u << 18)) & ~(1u << 3));
	nusb_phy_wr(0x68, dtm0 | (1u << 18) | (1u << 3));
	delay(30);
	/* Release forced data, pulldowns, transceiver, suspend and termination. */
	uint32_t release = (1u << 23) | (1u << 21) | (1u << 20) |
	    (1u << 19) | (1u << 18) | (1u << 17) | (0xfu << 10) |
	    (1u << 7) | (1u << 6) | (3u << 4) | (1u << 3) | (1u << 2);
	nusb_phy_wr(0x68, (dtm0 | (1u << 3)) & ~(1u << 18));
	nusb_phy_wr(0x68, dtm0 & ~release);
	/* USB PLL interrupt enable; disable 100uA SSUSB current and GPIO mode. */
	nusb_phy_wr(0, nusb_phy_rd(0) | (1u << 5));
	nusb_phy_wr(0x14, nusb_phy_rd(0x14) & ~(1u << 11));
	nusb_phy_wr(0x20, nusb_phy_rd(0x20) & ~((1u << 9) | (1u << 8)));
	/* Clear the explicit PHY pullup test override, as DPPULLUP_CLR does. */
	nusb_phy_wr(0x1c, nusb_phy_rd(0x1c) & ~((1u << 12) | (1u << 9)));
	/* Hand DP/DM back from BC1.1 detection; enable VBUS comparator. */
	nusb_phy_wr(0x18, (nusb_phy_rd(0x18) & ~(1u << 23)) | (1u << 20));
	uint32_t dtm1 = nusb_phy_rd(0x6c) & ~((1u << 16) | (1u << 4));
	/* Device ID, VBUS/AVALID values, as Linux set_mode/device + power_on. */
	nusb_phy_wr(0x6c, dtm1 | (1u << 9) | (1u << 1) | (1u << 5) | (1u << 2));
	delay(800);
}

static void
nusb_phy_restore(void)
{
	if (!nusb_phy.owned) { return; }
	/* Restore suspend/termination overrides last, after the analog registers. */
	for (unsigned i = 0; i < 7; i++) {
		if (nusb_phy_offsets[i] != 0x68) {
			nusb_phy_wr(nusb_phy_offsets[i], nusb_phy.saved[i]);
		}
	}
	nusb_phy_wr(0x68, nusb_phy.saved[5]);
	nusb_phy.owned = false;
}

/* No DMA was started. Disconnect before restoring owned PHY and MAC state. */
int
nord2_usb_prepare_return(void)
{
	if (!nusb.owned) { return 0; }
	/* Normal timeout cleanup also runs on CPU0. Panic cleanup is reached
	 * only after the SMP return guard has stopped every other core. */
	if (nusb_smp && ((__builtin_arm_rsr64("MPIDR_EL1") >> 8) & 0xff)) {
		return -1;
	}
	nusb_wr(0x2404, nusb_rd(0x2404) & ~NUSB_SOFTCON);
	nusb_wr(0xc, ~0u);
	nusb_wr(0x8c, ~0u);
	nusb_wr(0x80, ~0u);
	nusb_wr(0x241c, ~0u);
	nusb_wr(0x854, ~0u);
	nusb_wr(0x100, 64);
	nusb_wr(0x800, nusb.devconf);
	nusb_phy_restore();
	nusb_wr(0x2418, nusb.commonie);
	nusb_wr(0x850, nusb.linkie);
	nusb_wr(0x2404, nusb.power);
	nusb_wr(0x88, nusb.epie);
	nusb_wr(8, nusb.lv1ie);
	nusb.owned = false;
	return 1;
}

static bool
nusb_poll_continue(bool background, unsigned stop, uint64_t now, uint64_t deadline)
{
	return (background && stop == 23) || now < deadline;
}

static void
nord2_usb_enumerate(vm_offset_t mac, bool prepare_phy, bool background)
{
	uint8_t data[1024], setup[8];
	uint32_t sent = 0, length = 0;
	bool sending = false, zlp = false;
	nusb.mac = mac;
	uint32_t cap = nusb_rd(0xc10);
	unsigned tx = cap & 0x1f, rx = (cap >> 8) & 0x1f;
	if (tx > 15 || rx > 15 || !tx || !rx) {
		printf("NORD2 USB: invalid endpoint capability\n"); return;
	}
	for (unsigned i = 1; i <= MAX(tx, rx); i++) {
		if ((i <= tx && (nusb_rd(0x500 + i * 16) & (1u << 15))) ||
		    (i <= rx && (nusb_rd(0x600 + i * 16) & (1u << 15)))) {
			printf("NORD2 USB: inherited QMU active; refused\n"); return;
		}
	}
	nusb.power = nusb_rd(0x2404);
	if (nusb.power & NUSB_SOFTCON) {
		printf("NORD2 USB: inherited connection active; refused\n"); return;
	}
	volatile uint32_t *wdt = (volatile uint32_t *)ml_io_map(0x10007000, 0x1000);
	uint64_t frequency = __builtin_arm_rsr64("CNTFRQ_EL0");
	if (!wdt || !frequency || frequency > 1000000000) {
		printf("NORD2 USB: no watchdog/timer; refused\n"); return;
	}
	if (prepare_phy && !nusb_phy_snapshot()) {
		printf("NORD2 USB: PHY snapshot unavailable; refused\n"); return;
	}
	printf("NORD2 USB: connect pre POWER=%08x DEV=%08x TEST=%08x USB3=%08x\n",
	    nusb.power, nusb_rd(0x800), nusb_rd(0x2414), nusb_rd(0x141c));
	nusb.lv1ie = nusb_rd(4);
	nusb.epie = nusb_rd(0x84);
	nusb.commonie = nusb_rd(0x2418);
	nusb.linkie = nusb_rd(0x850);
	nusb.devconf = nusb_rd(0x800);
	nusb.configuration = 0;
	nusb.requests = nusb.resets = nusb.log_reads = nusb.stalls = 0;
	if (background && nusb_background_stop == 23) {
		printf("NORD2 USB: native console continuous; EP0 PIO, no DMA\n");
	} else {
		printf("NORD2 USB: native console for 45 seconds; EP0 PIO, no DMA\n");
	}
	uint64_t daif;
	__asm__ volatile("mrs %0, daif\nmsr daifset, #3" : "=r"(daif) :: "memory");
	nusb.owned = true;
	nusb_wr(0xc, ~0u);     /* Poll status, do not deliver MAC IRQs to XNU. */
	nusb_wr(0x8c, ~0u);
	nusb_wr(0x80, ~0u);
	nusb_wr(0x241c, ~0u);
	nusb_wr(0x854, ~0u);
	nusb_wr(0x800, 0);
	nusb_wr(0x100, 64);
	nusb_wr(0x88, 0x10001);
	if (prepare_phy) {
		nusb_phy_enable();
	}
	nusb_wr(0x2404, nusb.power | NUSB_SOFTCON | (1u << 5) | 1);
	uint32_t connect_power = nusb_rd(0x2404);
	uint32_t phy_dtm0 = prepare_phy ? nusb_phy_rd(0x68) : 0;
	uint32_t phy_dtm1 = prepare_phy ? nusb_phy_rd(0x6c) : 0;
	uint32_t phy_acr6 = prepare_phy ? nusb_phy_rd(0x18) : 0;
	uint64_t deadline = __builtin_arm_rsr64("CNTVCT_EL0") + frequency * 45;
	if (background) {
		__atomic_store_n(&nusb_background_state, 1, __ATOMIC_RELEASE);
		__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
	}
	while (nusb_poll_continue(background, nusb_background_stop,
	    __builtin_arm_rsr64("CNTVCT_EL0"), deadline)) {
		if (background) {
			/* Keep one PIO iteration atomic, then let the timer/BSD run. */
			__asm__ volatile("msr daifset, #3" ::: "memory");
		}
		wdt[2] = 0x1971;
		uint32_t common = nusb_rd(0x241c);
		if (common) { nusb_wr(0x241c, common); }
		if (common & 4) {
			nusb.resets++;
			nusb.configuration = 0;
			sending = false;
			nusb_wr(0x800, nusb_rd(0x800) & ~(0x7fu << 24));
			nusb_wr(0x100, 64);
		}
		uint32_t event = nusb_rd(0x80) & 0x10001;
		if (!event) { goto poll_done; }
		nusb_wr(0x80, event);
		if (event & 0x10000) { sending = false; }
		uint32_t csr = nusb_rd(0x100);
		if (csr & (1u << 22)) {
			nusb_wr(0x100, ((csr & ~NUSB_CSR_W1C) & ~NUSB_STALL) | (1u << 22));
			sending = false;
			csr = nusb_rd(0x100);
		}
		if (csr & NUSB_SETUP) {
			sending = false;
			uint32_t count = nusb_rd(0x108);
			if (count != 8) {
				nusb.stalls++;
				nusb_wr(0x100, (csr & ~NUSB_CSR_W1C) | NUSB_STALL | NUSB_SETUP);
				goto poll_done;
			}
			for (unsigned i = 0; i < 8; i += 4) {
				uint32_t word = nusb_rd(0x300); memcpy(setup + i, &word, 4);
			}
			nusb.requests++;
			int address, configuration;
			int response = nusb_reply(setup, data, &length, &address, &configuration);
			csr &= ~NUSB_CSR_W1C;
			if (!response) {
				/* Data direction is latched when SETUP is acknowledged. */
				if (nusb_le16(setup + 6)) {
					nusb_wr(0x100, (csr | NUSB_SETUP) &
					    ~NUSB_DIR_IN);
					csr = nusb_rd(0x100) & ~NUSB_CSR_W1C;
					nusb_wr(0x100, csr | NUSB_STALL);
				} else {
					nusb_wr(0x100, csr | NUSB_SETUP | NUSB_STALL);
				}
				nusb.stalls++;
			} else if (response == 2) {
				if (address >= 0) {
					nusb_wr(0x800, (nusb_rd(0x800) & ~(0x7fu << 24)) |
					    (uint32_t)address << 24);
				}
				if (configuration >= 0) { nusb.configuration = (uint8_t)configuration; }
				nusb_wr(0x100, csr | NUSB_SETUP | NUSB_END);
			} else {
				nusb_wr(0x100, csr | NUSB_SETUP | NUSB_DIR_IN);
				sent = 0; sending = true;
				zlp = length < nusb_le16(setup + 6) && length % 64 == 0;
				/* The first IN packet is armed immediately. */
				uint32_t count = MIN(length, 64u);
				nusb_fifo_write(data, count); sent = count;
				if (!count) { zlp = false; }
				nusb_wr(0x100, (nusb_rd(0x100) & ~NUSB_CSR_W1C) | NUSB_TX);
			}
		} else if (sending && !(csr & NUSB_TX)) {
			if (sent < length || zlp) {
				uint32_t count = MIN(length - sent, 64u);
				nusb_fifo_write(data + sent, count); sent += count;
				if (!count) { zlp = false; }
				nusb_wr(0x100, (csr & ~NUSB_CSR_W1C) | NUSB_TX);
			} else {
				nusb_wr(0x100, (csr & ~NUSB_CSR_W1C) | NUSB_END);
				sending = false;
			}
		}
 poll_done:
		if (background) {
			__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
			nusb_sleep_ms(1);
		}
	}
	if (background) {
		__asm__ volatile("msr daifset, #3" ::: "memory");
	}
	uint32_t final_power = nusb_rd(0x2404), final_dev = nusb_rd(0x800);
	uint32_t final_opstate = nusb_rd(0x2460), final_csr = nusb_rd(0x100);
	if (nord2_usb_prepare_return() < 0) {
		panic("NORD2 USB: cleanup attempted off CPU0");
	}
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
	printf("NORD2 USB: connect POWER=%08x PHY active DTM0=%08x DTM1=%08x ACR6=%08x\n",
	    connect_power, phy_dtm0, phy_dtm1, phy_acr6);
	printf("NORD2 USB: final POWER=%08x DEV=%08x OPSTATE=%08x EP0=%08x\n",
	    final_power, final_dev, final_opstate, final_csr);
	printf("NORD2 USB: native console done: resets=%u requests=%u log_reads=%u stalls=%u\n",
	    nusb.resets, nusb.requests, nusb.log_reads, nusb.stalls);
}

static void
nusb_background_thread(void *arg, wait_result_t wr __unused)
{
	(void)thread_bind(cpu_to_processor(0));
	thread_block(THREAD_CONTINUE_NULL);
	if (cpu_number() != 0) {
		panic("NORD2 USB: service did not reach CPU0");
	}
	printf("NORD2 USB: service bound to CPU0\n");
	nord2_usb_enumerate((vm_offset_t)arg, true, true);
	__atomic_store_n(&nusb_background_state, 2, __ATOMIC_RELEASE);
	/* Diagnostic modes time out; mode23 returns only if setup was refused. */
	if (nusb_background_stop == 23 && !n2usb_strict) {
		/* 0700: a refused/ended console no longer takes the running system down. */
		n2usb_state = n2usb_state == 1 || n2usb_state == 3 ? 5 : 4;
		printf("NORD2 USB14: console service ended (state %u); iOS keeps running without USB console\n", n2usb_state);
	} else {
		nord2_stop_at(nusb_background_stop, "after background MTU3 USB console");
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

/* ---- 0700: boot without a host, late console ---- */
static const char *const n2usb_why_name[] = {"none", "SMP not ready", "mapping failed", "USB clocks gated (no cable at power-on)",
    "USB IP inactive", "thread creation failed", "console start timed out", "cpus != 8", "console refused"};

/* Read-only check of the same gates nord2_usb_probe() checks; 0 = the MAC can be used (mac returned). */
static unsigned
n2usb_gate_check(vm_offset_t *mac_out, uint32_t r[6])
{
	static vm_offset_t infra, top, ippc, mac;
	r[0] = r[1] = r[2] = r[3] = r[4] = r[5] = 0;
	if (!infra) { infra = ml_io_map(0x10001000, 0x1000); }
	if (!top) { top = ml_io_map(0x10000000, 0x1000); }
	if (!infra || !top) { return N2USB_WHY_MAP; }
	r[0] = nord2_usb_read(infra, 0xac);
	r[1] = nord2_usb_read(top, 0xa0);
	r[2] = nord2_usb_read(top, 0xb0);
	if ((r[0] & ((1u << 1) | (1u << 31))) || (r[1] & (1u << 31)) || (r[2] & (1u << 7))) { return N2USB_WHY_CLOCK; }
	if (!ippc) { ippc = ml_io_map(0x11203e00, 0x100); }
	if (!ippc) { return N2USB_WHY_MAP; }
	r[3] = nord2_usb_read(ippc, 8);
	r[4] = nord2_usb_read(ippc, 0x14);
	r[5] = nord2_usb_read(ippc, 0x50);
	if ((r[3] & 1) || !(r[4] & 1) || (r[5] & 7)) { return N2USB_WHY_INACTIVE; }
	if (!mac) { mac = ml_io_map(0x11201000, 0x2e00); }
	if (!mac) { return N2USB_WHY_MAP; }
	*mac_out = mac;
	return N2USB_WHY_NONE;
}

static void
n2usb_wr32(vm_offset_t base, uint32_t offset, uint32_t value)
{
	__asm__ volatile("dmb oshst" ::: "memory");
	*(volatile uint32_t *)(base + offset) = value;
	__asm__ volatile("dsb osh" ::: "memory");
}

/*
 * kern.n2usb_late=2 only (EXPERIMENTAL): what the preloader does when it sees a cable, Linux order (clk-mt6885.c gates,
 * mtu3 ssusb_ip_sw_reset/mtu3_device_enable without the IP reset):  usb_sel PDN (CLK_CFG_9 bit 31) and ssusb_xhci_sel PDN
 * (CLK_CFG_10 bit 7) cleared through the _CLR registers + CLK_CFG_UPDATE1 bits 8/9; INFRA_CG3 ifrao_ssusb (1) and
 * ifrao_ssusb_xhci (31) ungated through CG3_CLR 0xa8; IPPC: device IP power on (CTRL2 bit 0 = 0), host IP down (CTRL1 bit 0),
 * U2 port enabled/powered/device (U2_CTRL_P0 bits 0..2 = 0); wait for SYSPLL stable (STS1 bit 0) and U2 MAC reset release
 * (STS2 bit 0), at most 200 ms.  Never touches the PHY analog, regulators, the MT6360 USB switch or the charger.
 */
static void
n2usb_power_up(void)
{
	vm_offset_t top = ml_io_map(0x10000000, 0x1000), infra = ml_io_map(0x10001000, 0x1000);
	if (!top || !infra) { printf("NORD2 USB14: power-up mapping failed\n"); return; }
	uint32_t before = nord2_usb_read(infra, 0xac);
	n2usb_wr32(top, 0xa8, 1u << 31);
	n2usb_wr32(top, 0x8, 1u << 8);
	n2usb_wr32(top, 0xb8, 1u << 7);
	n2usb_wr32(top, 0x8, 1u << 9);
	delay(20);
	n2usb_wr32(infra, 0xa8, (1u << 1) | (1u << 31));
	delay(20);
	uint32_t cg = nord2_usb_read(infra, 0xac), c9 = nord2_usb_read(top, 0xa0), c10 = nord2_usb_read(top, 0xb0);
	if ((cg & ((1u << 1) | (1u << 31))) || (c9 & (1u << 31)) || (c10 & (1u << 7))) {
		printf("NORD2 USB14: power-up: clocks still gated CG3 %08x->%08x CFG9=%08x CFG10=%08x\n", before, cg, c9, c10);
		return;
	}
	vm_offset_t ippc = ml_io_map(0x11203e00, 0x100);
	if (!ippc) { printf("NORD2 USB14: power-up IPPC mapping failed\n"); return; }
	n2usb_wr32(ippc, 8, nord2_usb_read(ippc, 8) & ~1u);
	n2usb_wr32(ippc, 4, nord2_usb_read(ippc, 4) | 1u);
	n2usb_wr32(ippc, 0x50, nord2_usb_read(ippc, 0x50) & ~7u);
	uint32_t s1 = 0, s2 = 0;
	unsigned ms = 0;
	for (; ms < 200; ms++) {
		s1 = nord2_usb_read(ippc, 0x10); s2 = nord2_usb_read(ippc, 0x14);
		if ((s1 & 1) && (s2 & 1)) { break; }
		nusb_sleep_ms(1);
	}
	printf("NORD2 USB14: power-up clocks CG3 %08x->%08x, IPPC ctrl2=%08x u2=%08x sts1=%08x sts2=%08x after %u ms%s\n",
	    before, cg, nord2_usb_read(ippc, 8), nord2_usb_read(ippc, 0x50), s1, s2, ms, ms >= 200 ? " (NOT stable)" : "");
}

static bool
n2usb_start_console(vm_offset_t mac)
{
	thread_t thread;
	nusb_smp = true;
	nusb_background_stop = 23;
	__atomic_store_n(&nusb_background_state, 0, __ATOMIC_RELEASE);
	if (kernel_thread_start(nusb_background_thread, (void *)mac, &thread) != KERN_SUCCESS) { return false; }
	thread_deallocate(thread);
	for (unsigned i = 0; !__atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE) && i < 10000; i++) {
		nusb_sleep_ms(1);
	}
	return __atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE) == 1;
}

static void
n2usb_late_thread(void *arg __unused, wait_result_t wr __unused)
{
	unsigned last = ~0u;
	for (;;) {
		nusb_sleep_ms(2000);
		if (!n2usb_late) { continue; }
		n2usb_late_checks++;
		vm_offset_t mac = 0;
		uint32_t r[6];
		unsigned why = n2usb_gate_check(&mac, r);
		uint32_t vbus = nord2_chg_vbus_good;
		if (why != last || (n2usb_late_checks % 150) == 0) {
			printf("NORD2 USB14: late check %u why=%u CG3=%08x CFG9=%08x CFG10=%08x IPPC pw=%08x sts2=%08x u2=%08x vbus=%u late=%u\n",
			    n2usb_late_checks, why, r[0], r[1], r[2], r[3], r[4], r[5], vbus, n2usb_late);
			last = why;
		}
		if (why == N2USB_WHY_NONE) {
			if (n2usb_start_console(mac)) {
				n2usb_state = 3;
				printf("NORD2 USB14: late console up after %u checks (%u power-ups)\n", n2usb_late_checks, n2usb_powerups);
			} else {
				printf("NORD2 USB14: late console start failed (state %u); late thread ends\n",
				    __atomic_load_n(&nusb_background_state, __ATOMIC_ACQUIRE));
			}
			break;
		}
		if ((why == N2USB_WHY_CLOCK || why == N2USB_WHY_INACTIVE) && n2usb_late >= 2 && vbus == 1 && n2usb_powerups < 3) {
			n2usb_powerups++;
			n2usb_power_up();
		}
	}
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

static void
n2usb_no_host_boot(unsigned why)
{
	n2usb_state = 2;
	printf("NORD2 USB14: no usable USB at boot (why=%u: %s); booting on without the USB console (kern.n2usb_strict=1 restores the stop)\n",
	    why, why < sizeof(n2usb_why_name) / sizeof(n2usb_why_name[0]) ? n2usb_why_name[why] : "?");
	if (why == N2USB_WHY_TIMEOUT || why == N2USB_WHY_REFUSED || why == N2USB_WHY_CPUS) {
		return;   /* a console thread exists or the configuration is wrong: no second owner */
	}
	thread_t thread;
	if (kernel_thread_start(n2usb_late_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	} else {
		printf("NORD2 USB14: late thread not started\n");
	}
}

/* For the charger policy (0702): SETUP requests seen on EP0 since the console started (0 = no console).  Only SETUPs: a wall
 * charger (DCP, D+/D- shorted) can make the MAC report bus resets, but never sends a SETUP packet. */
uint32_t nord2_usb_host_events(void);
uint32_t
nord2_usb_host_events(void)
{
	return nusb.owned ? nusb.requests : 0;
}
uint32_t nord2_usb_configured(void);
uint32_t
nord2_usb_configured(void)
{
	return nusb.owned && nusb.configuration ? 1u : 0u;
}
uint32_t nord2_usb_console_state(void);
uint32_t
nord2_usb_console_state(void)
{
	return n2usb_state;
}
#else /* Qualcomm DWC3; never accessed on Nord2. */

extern vm_offset_t ml_io_map(vm_offset_t, vm_size_t);
void sweet_usb_probe(void);
static void sweet_usb_gadget_start(vm_offset_t dwc);
extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);

#define GCC_PA                  0x00100000
#define GCC_SIZE                0x00090000
#define GCC_USB30_PRIM_GDSCR    0x0f004         /* bit 31: power on */
#define CBCR_CLK_OFF            (1u << 31)

static const struct {
	const char *name;
	uint32_t    off;
} usb_cbcr[] = {
	{ "master", 0x0f010 },
	{ "sleep", 0x0f014 },
	{ "utmi", 0x0f018 },
	{ "cfg-noc-axi", 0x0502c },
	{ "aggre-axi", 0x8201c },
	{ "ahb2phy", 0x6a004 },
	{ "clkref", 0x8c010 },
};

#define DWC3_PA                 0x0a600000
#define DWC3_SIZE               0x0000d000
#define QUSB2_PA                0x088e2000
#define QUSB2_SIZE              0x00000400

static uint32_t
usb_rd(vm_offset_t base, uint32_t off)
{
	uint32_t v = *(volatile uint32_t *)(base + off);
	__asm__ volatile ("dmb oshld" ::: "memory");
	return v;
}

void
sweet_usb_probe(void)
{
	int enabled = 1;
	uint32_t gdscr, cbcr[sizeof(usb_cbcr) / sizeof(usb_cbcr[0])];
	bool clocks_on = true;

	/* sweetusb=0 off, 1 (default) report state only, 2 also start the USB device */
	PE_parse_boot_argn("sweetusb", &enabled, sizeof(enabled));
	if (!enabled) {
		return;
	}
	vm_offset_t gcc = ml_io_map(GCC_PA, GCC_SIZE);
	if (gcc == 0) {
		printf("SWEET USB: GCC map failed\n");
		return;
	}
	gdscr = usb_rd(gcc, GCC_USB30_PRIM_GDSCR);
	printf("SWEET USB: GDSC usb30_prim 0x%08x (%s)\n", gdscr, (gdscr & (1u << 31)) ? "on" : "off");
	for (unsigned i = 0; i < sizeof(usb_cbcr) / sizeof(usb_cbcr[0]); i++) {
		cbcr[i] = usb_rd(gcc, usb_cbcr[i].off);
		printf("SWEET USB: clk %-12s CBCR 0x%08x (%s)\n", usb_cbcr[i].name, cbcr[i],
		    (cbcr[i] & CBCR_CLK_OFF) ? "off" : "on");
	}
	/* DWC3 registers need the GDSC, the master clock and the config-NoC path. */
	clocks_on = (gdscr & (1u << 31)) && !(cbcr[0] & CBCR_CLK_OFF) && !(cbcr[3] & CBCR_CLK_OFF);
	if (!clocks_on) {
		printf("SWEET USB: controller unpowered or unclocked; DWC3 not read\n");
		return;
	}
	vm_offset_t dwc = ml_io_map(DWC3_PA, DWC3_SIZE);
	if (dwc == 0) {
		printf("SWEET USB: DWC3 map failed\n");
		return;
	}
	printf("SWEET USB: DWC3 GSNPSID 0x%08x GCTL 0x%08x GSTS 0x%08x GUSB2PHYCFG 0x%08x\n",
	    usb_rd(dwc, 0xc120), usb_rd(dwc, 0xc110), usb_rd(dwc, 0xc118), usb_rd(dwc, 0xc200));
	printf("SWEET USB: DWC3 DCFG 0x%08x DCTL 0x%08x DEVTEN 0x%08x DSTS 0x%08x DALEPENA 0x%08x\n",
	    usb_rd(dwc, 0xc700), usb_rd(dwc, 0xc704), usb_rd(dwc, 0xc708), usb_rd(dwc, 0xc70c),
	    usb_rd(dwc, 0xc720));
	printf("SWEET USB: DWC3 GEVNTADR 0x%08x%08x GEVNTSIZ 0x%08x GEVNTCOUNT 0x%08x GHWPARAMS0 0x%08x\n",
	    usb_rd(dwc, 0xc404), usb_rd(dwc, 0xc400), usb_rd(dwc, 0xc408), usb_rd(dwc, 0xc40c),
	    usb_rd(dwc, 0xc140));
	if (enabled >= 2) {
		sweet_usb_gadget_start(dwc);
	}
	if (cbcr[5] & CBCR_CLK_OFF) {
		printf("SWEET USB: ahb2phy clock off; QUSB2 PHY not read\n");
		return;
	}
	vm_offset_t phy = ml_io_map(QUSB2_PA, QUSB2_SIZE);
	if (phy == 0) {
		return;
	}
	/* DT qcom,qusb-phy-reg-offset order (Linux phy-msm-qusb-v2.c): PORT_TUNE1 0x240,
	 * PLL_COMMON_STATUS_ONE 0x1a0, PWR_CTRL1 0x210, INTR_CTRL 0x230,
	 * PLL_CORE_INPUT_OVERRIDE 0x0a8, TEST1 0x254. */
	printf("SWEET USB: QUSB2 PLL_STATUS 0x%08x PWR_CTRL1 0x%08x PORT_TUNE1 0x%08x TEST1 0x%08x\n",
	    usb_rd(phy, 0x1a0), usb_rd(phy, 0x210), usb_rd(phy, 0x240), usb_rd(phy, 0x254));
}

/*
 * Step 2: USB device mode with only the default control endpoint, enough
 * for the host to enumerate the phone (boot-arg sweetusb=2). ABL leaves
 * the DWC3 powered, clocked, in device mode and halted, with the PHY set
 * up, so only the device controller is restarted: soft reset, an event
 * buffer in our memory, endpoint 0, run. Events are polled from a kernel
 * thread (no interrupt), like the UFS driver. DMA is not cache coherent
 * (DT qcom,gsi-disable-io-coherency): buffers are cleaned/invalidated.
 * Register layout and the control-transfer state machine follow the
 * Synopsys DWC3 databook as used by Linux drivers/usb/dwc3 (gadget.c,
 * ep0.c).
 */
#define GCTL            0xc110
#define GEVNTADRLO      0xc400
#define GEVNTADRHI      0xc404
#define GEVNTSIZ        0xc408
#define GEVNTCOUNT      0xc40c
#define DCFG            0xc700
#define DCTL            0xc704
#define DEVTEN          0xc708
#define DSTS            0xc70c
#define DALEPENA        0xc720
#define DEPCMDPAR2(n)   (0xc800 + 0x10 * (n))
#define DEPCMDPAR1(n)   (0xc804 + 0x10 * (n))
#define DEPCMDPAR0(n)   (0xc808 + 0x10 * (n))
#define DEPCMD(n)       (0xc80c + 0x10 * (n))

/*
 * Qualcomm wrapper registers ("QSCRATCH", core base + 0xf8800). The
 * controller only connects (pull-up on D+) while it sees a valid VBUS
 * session; Linux dwc3-qcom forces that in software when the charger
 * reports a cable (dwc3_qcom_vbus_override_enable).
 */
#define QSCRATCH_PA             0x0a6f8800
#define QSCRATCH_HS_PHY_CTRL    0x10
#define   UTMI_OTG_VBUS_VALID   (1u << 20)
#define   SW_SESSVLD_SEL        (1u << 28)
#define QSCRATCH_SS_PHY_CTRL    0x30
#define   LANE0_PWR_PRESENT     (1u << 24)

#define DCTL_RUN_STOP   (1u << 31)
#define DCTL_CSFTRST    (1u << 30)
#define DSTS_DEVCTRLHLT (1u << 22)

#define DEPCMD_SETEPCONFIG      1
#define DEPCMD_SETTRANSFRESOURCE 2
#define DEPCMD_SETSTALL         4
#define DEPCMD_STARTTRANSFER    6
#define DEPCMD_DEPSTARTCFG      9
#define DEPCMD_CMDACT           (1u << 10)

#define TRB_HWO         (1u << 0)
#define TRB_LST         (1u << 1)
#define TRB_ISP_IMI     (1u << 10)
#define TRB_IOC         (1u << 11)
#define TRBCTL_SETUP    (2u << 4)
#define TRBCTL_STATUS2  (3u << 4)
#define TRBCTL_STATUS3  (4u << 4)
#define TRBCTL_DATA     (5u << 4)

/* DMA area layout */
#define UD_EVT          0x0000          /* event buffer, 4 KiB */
#define UD_EVT_SIZE     0x1000
#define UD_TRB_OUT      0x1000          /* ep0 OUT TRB */
#define UD_TRB_IN       0x1010          /* ep0 IN TRB */
#define UD_TRB_RX       0x1020          /* ep1 OUT (bulk, host -> phone) */
#define UD_TRB_TX       0x1030          /* ep1 IN (bulk, phone -> host) */
#define UD_TRB_INT      0x1050          /* ep2 IN (interrupt, notifications) */
#define UD_SETUP        0x1040          /* 8-byte setup packet */
#define UD_DATA         0x1100          /* ep0 data stage, 512 bytes */
#define UD_NOTIFY       0x1300          /* 16-byte notification */
#define UD_RX           0x2000          /* one received frame */
#define UD_TX           0x2800          /* one frame to send */
#define UD_FRAME_MAX    0x800
#define UD_SIZE         0x4000

/* Physical endpoints: 2n = OUT, 2n+1 = IN */
#define EP_RX           2               /* 0x01 bulk OUT */
#define EP_TX           3               /* 0x81 bulk IN */
#define EP_INT          5               /* 0x82 interrupt IN */
#define TRBCTL_NORMAL   (1u << 4)

#define UD_TXQ          32

static struct {
	vm_offset_t dwc;
	vm_offset_t dma;
	uint64_t    dma_pa;
	uint32_t    evt_pos;
	int         sleep_chan;
	enum { EP0_SETUP, EP0_DATA, EP0_WAIT_STATUS2, EP0_WAIT_STATUS3, EP0_STATUS } state;
	bool        data_in;        /* data stage direction */
	uint32_t    events;
	bool        configured;
	uint8_t     data_alt;       /* data interface alternate setting */
	/* network */
	ifnet_t     ifp;
	lck_mtx_t  *lock;           /* TX ring and TX endpoint */
	mbuf_t      txq[UD_TXQ];
	unsigned    txq_head, txq_count;
	bool        tx_busy;
	uint64_t    rx_frames, tx_frames;
} ud;

static inline void
dwc_wr(uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(ud.dwc + off) = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static inline uint32_t
dwc_rd(uint32_t off)
{
	return usb_rd(ud.dwc, off);
}

static void
ud_sync(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(ud.dma_pa + off), len, TRUE);
}

static void
ud_sleep_ms(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&ud.sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

static int
ud_epcmd(unsigned ep, uint32_t cmd, uint32_t p0, uint32_t p1, uint32_t p2)
{
	dwc_wr(DEPCMDPAR0(ep), p0);
	dwc_wr(DEPCMDPAR1(ep), p1);
	dwc_wr(DEPCMDPAR2(ep), p2);
	dwc_wr(DEPCMD(ep), cmd | DEPCMD_CMDACT);
	for (int i = 0; i < 1000; i++) {
		uint32_t v = dwc_rd(DEPCMD(ep));
		if (!(v & DEPCMD_CMDACT)) {
			uint32_t status = (v >> 12) & 0xf;
			if (status) {
				printf("SWEET USB: ep%u cmd 0x%x status %u\n", ep, cmd & 0xf, status);
				return -1;
			}
			return 0;
		}
		delay(1);
	}
	printf("SWEET USB: ep%u cmd 0x%x timeout\n", ep, cmd & 0xf);
	return -1;
}

/* One TRB (at DMA offset trb) on physical endpoint ep. */
static void
ud_start_trb(unsigned ep, uint32_t trb, uint32_t buf, uint32_t len, uint32_t ctl)
{
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + trb);
	uint64_t bpa = ud.dma_pa + buf;

	t[0] = (uint32_t)bpa;
	t[1] = (uint32_t)(bpa >> 32);
	t[2] = len;
	t[3] = ctl | TRB_HWO | TRB_LST | TRB_ISP_IMI | TRB_IOC;
	ud_sync(trb, 16);
	if (len) {
		ud_sync(buf, len);
	}
	uint64_t tpa = ud.dma_pa + trb;
	ud_epcmd(ep, DEPCMD_STARTTRANSFER, (uint32_t)(tpa >> 32), (uint32_t)tpa, 0);
}

static void
ud_start(unsigned ep, uint32_t buf, uint32_t len, uint32_t ctl)
{
	ud_start_trb(ep, ep ? UD_TRB_IN : UD_TRB_OUT, buf, len, ctl);
}

static void
ud_start_setup(void)
{
	ud.state = EP0_SETUP;
	ud_start(0, UD_SETUP, 8, TRBCTL_SETUP);
}

static void
ud_stall(void)
{
	ud_epcmd(0, DEPCMD_SETSTALL, 0, 0, 0);
	ud_start_setup();
}

/*
 * USB descriptors: a CDC-ECM (Ethernet) function, which Linux (cdc_ether)
 * and macOS drive without extra drivers. VID 0x1209 / PID 0x0001 =
 * pid.codes test ID, not a product.
 */
static const uint8_t ud_dev_desc[18] = {
	18, 1, 0x00, 0x02, 0x02, 0x00, 0x00, 64,
	0x09, 0x12, 0x01, 0x00, 0x00, 0x01, 1, 2, 3, 1,
};
static const uint8_t ud_cfg_desc[80] = {
	9, 2, 80, 0, 2, 1, 0, 0x80, 250,                /* config: 2 interfaces, 500 mA */
	9, 4, 0, 0, 1, 0x02, 0x06, 0x00, 0,             /* comm interface 0: CDC ECM, 1 endpoint */
	5, 0x24, 0x00, 0x10, 0x01,                      /* header, CDC 1.10 */
	5, 0x24, 0x06, 0, 1,                            /* union: control 0, data 1 */
	13, 0x24, 0x0f, 4, 0, 0, 0, 0, 0xea, 0x05, 0, 0, 0, /* ethernet: MAC string 4, max segment 1514 */
	7, 5, 0x82, 0x03, 16, 0, 9,                     /* ep 0x82 interrupt IN, 16 bytes, 2^(9-1) uframes */
	9, 4, 1, 0, 0, 0x0a, 0x00, 0x00, 0,             /* data interface 1 alt 0: no endpoints */
	9, 4, 1, 1, 2, 0x0a, 0x00, 0x00, 0,             /* data interface 1 alt 1: two bulk endpoints */
	7, 5, 0x81, 0x02, 0x00, 0x02, 0,                /* ep 0x81 bulk IN, 512 */
	7, 5, 0x01, 0x02, 0x00, 0x02, 0,                /* ep 0x01 bulk OUT, 512 */
};
/* The host side's MAC (ECM iMACAddress); the phone's own is ..:02 */
static const char *const ud_strings[] = {
	NULL, "ios-sweet", "Sweet iOS (USB Ethernet)", "0001", "02005E77EE01",
};
static const uint8_t ud_phone_mac[ETHER_ADDR_LEN] = { 0x02, 0x00, 0x5e, 0x77, 0xee, 0x02 };

static uint32_t
ud_string(unsigned idx, uint8_t *out)
{
	if (idx == 0) {
		out[0] = 4; out[1] = 3; out[2] = 0x09; out[3] = 0x04;   /* en-US */
		return 4;
	}
	if (idx >= sizeof(ud_strings) / sizeof(ud_strings[0])) {
		return 0;
	}
	const char *str = ud_strings[idx];
	uint32_t n = 2;
	for (; *str && n < 126; str++) {
		out[n++] = (uint8_t)*str;
		out[n++] = 0;
	}
	out[0] = (uint8_t)n;
	out[1] = 3;
	return n;
}

static void ud_rx_arm(void);
static void ud_tx_kick_locked(void);
static void ud_link_event(bool up);

/* NETWORK_CONNECTION notification: the host brings its link up on it. */
static void
ud_notify_connected(void)
{
	uint8_t *n = (uint8_t *)(ud.dma + UD_NOTIFY);
	n[0] = 0xa1; n[1] = 0x00;       /* NETWORK_CONNECTION */
	n[2] = 1; n[3] = 0;             /* connected */
	n[4] = 0; n[5] = 0;             /* interface 0 */
	n[6] = 0; n[7] = 0;
	ud_start_trb(EP_INT, UD_TRB_INT, UD_NOTIFY, 8, TRBCTL_NORMAL);
}

/* SET_CONFIGURATION(1): bulk IN/OUT and the interrupt endpoint. */
static void
ud_configure(void)
{
	if (ud.configured) {
		return;
	}
	if (ud_epcmd(0, DEPCMD_DEPSTARTCFG | (2u << 16), 0, 0, 0) ||
	    ud_epcmd(EP_RX, DEPCMD_SETEPCONFIG, (2u << 1) | (512u << 3), (1u << 8) | ((uint32_t)EP_RX << 25), 0) ||
	    ud_epcmd(EP_RX, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(EP_TX, DEPCMD_SETEPCONFIG, (2u << 1) | (512u << 3) | (1u << 17), (1u << 8) | ((uint32_t)EP_TX << 25), 0) ||
	    ud_epcmd(EP_TX, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(EP_INT, DEPCMD_SETEPCONFIG, (3u << 1) | (16u << 3) | (2u << 17),
	    (1u << 8) | (8u << 16) | ((uint32_t)EP_INT << 25), 0) ||
	    ud_epcmd(EP_INT, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		printf("SWEET USB: data endpoints setup failed\n");
		return;
	}
	dwc_wr(DALEPENA, dwc_rd(DALEPENA) | (1u << EP_RX) | (1u << EP_TX) | (1u << EP_INT));
	ud.configured = true;
	ud_rx_arm();
	printf("SWEET USB: ECM endpoints configured\n");
}

static void
ud_setup(void)
{
	const uint8_t *st = (const uint8_t *)(ud.dma + UD_SETUP);
	uint8_t *data = (uint8_t *)(ud.dma + UD_DATA);
	uint8_t type = st[0], req = st[1];
	uint16_t value = (uint16_t)(st[2] | st[3] << 8);
	uint16_t index = (uint16_t)(st[4] | st[5] << 8);
	uint16_t length = (uint16_t)(st[6] | st[7] << 8);
	uint32_t n = 0;
	bool ok = true;

	printf("SWEET USB: setup %02x %02x %04x %04x len %u\n", type, req, value, index, length);
	ud.data_in = (type & 0x80) != 0;
	if ((type & 0x60) == 0x20) {    /* class request (e.g. SET_ETHERNET_PACKET_FILTER) */
		ok = (length == 0);
	} else if ((type & 0x60) != 0) {
		ok = false;
	} else if (req == 6) {          /* GET_DESCRIPTOR */
		switch (value >> 8) {
		case 1: memcpy(data, ud_dev_desc, sizeof(ud_dev_desc)); n = sizeof(ud_dev_desc); break;
		case 2: memcpy(data, ud_cfg_desc, sizeof(ud_cfg_desc)); n = sizeof(ud_cfg_desc); break;
		case 3: n = ud_string(value & 0xff, data); ok = n != 0; break;
		default: ok = false; break;
		}
	} else if (req == 5) {          /* SET_ADDRESS: takes effect now (DWC3 handles the timing) */
		uint32_t dcfg = dwc_rd(DCFG) & ~(0x7fu << 3);
		dwc_wr(DCFG, dcfg | ((uint32_t)(value & 0x7f) << 3));
		printf("SWEET USB: address %u\n", value & 0x7f);
	} else if (req == 9) {          /* SET_CONFIGURATION */
		if (value == 1) {
			ud_configure();
		}
		printf("SWEET USB: configured (%u)\n", value);
	} else if (req == 8) {          /* GET_CONFIGURATION */
		data[0] = ud.configured ? 1 : 0; n = 1;
	} else if (req == 0) {          /* GET_STATUS */
		data[0] = 0; data[1] = 0; n = 2;
	} else if (req == 11) {         /* SET_INTERFACE */
		if (index == 1) {
			ud.data_alt = (uint8_t)value;
			if (value == 1 && ud.configured) {
				ud_notify_connected();
				ud_link_event(true);
			}
		}
	} else if (req == 10) {         /* GET_INTERFACE */
		data[0] = index == 1 ? ud.data_alt : 0; n = 1;
	} else if (req == 1 || req == 3) { /* CLEAR/SET_FEATURE (halt, remote wakeup) */
	} else {
		ok = false;
	}
	if (!ok) {
		ud_stall();
		return;
	}
	if (length == 0) {
		ud.state = EP0_WAIT_STATUS2;    /* status stage on XferNotReady */
		return;
	}
	ud.state = EP0_DATA;
	if (ud.data_in) {
		if (n > length) {
			n = length;
		}
		ud_start(1, UD_DATA, n, TRBCTL_DATA);
	} else {
		ud_start(0, UD_DATA, 64, TRBCTL_DATA);  /* OUT data is not used here */
	}
}

/* ---- network interface (ue0) ---- */

static void
ud_rx_arm(void)
{
	ud_start_trb(EP_RX, UD_TRB_RX, UD_RX, UD_FRAME_MAX, TRBCTL_NORMAL);
}

static void
ud_rx_done(void)
{
	volatile uint32_t *t = (volatile uint32_t *)(ud.dma + UD_TRB_RX);
	ud_sync(UD_TRB_RX, 16);
	uint32_t len = UD_FRAME_MAX - (t[2] & 0xffffff);
	ud_sync(UD_RX, UD_FRAME_MAX);
	if (ud.ifp != NULL && len >= ETHER_HDR_LEN && len <= ETHER_MAX_LEN) {
		/*
		 * One mbuf with a 2 KiB cluster holds any frame. (mbuf_allocpacket
		 * may return a first mbuf of length 0: stripping the header from
		 * it panicked "Failed mbuf validity check: len -14".)
		 */
		mbuf_t m = NULL;
		if (mbuf_gethdr(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) == 0 &&
		    mbuf_mclget(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) == 0 &&
		    (mbuf_flags(m) & MBUF_EXT) && mbuf_maxlen(m) >= len) {
			struct ifnet_stat_increment_param stats = {};
			memcpy(mbuf_data(m), (const void *)(ud.dma + UD_RX), len);
			mbuf_setlen(m, len);
			mbuf_pkthdr_setlen(m, len);
			void *frame_header = mbuf_data(m);
			mbuf_pkthdr_setrcvif(m, ud.ifp);
			mbuf_pkthdr_setheader(m, frame_header);
			mbuf_pkthdr_adjustlen(m, -ETHER_HDR_LEN);
			(void)mbuf_setdata(m, (char *)mbuf_data(m) + ETHER_HDR_LEN, mbuf_len(m) - ETHER_HDR_LEN);
			stats.packets_in = 1;
			stats.bytes_in = len;
			ud.rx_frames++;
			ifnet_input(ud.ifp, m, &stats);
		} else if (m != NULL) {
			mbuf_freem(m);
		}
	}
	ud_rx_arm();
}

/* Start the next queued frame if the IN endpoint is idle. ud.lock held. */
static void
ud_tx_kick_locked(void)
{
	while (!ud.tx_busy && ud.txq_count > 0 && ud.configured) {
		mbuf_t m = ud.txq[ud.txq_head];
		ud.txq_head = (ud.txq_head + 1) % UD_TXQ;
		ud.txq_count--;
		size_t len = mbuf_pkthdr_len(m);
		if (len > ETHER_MAX_LEN - ETHER_CRC_LEN) {
			mbuf_freem(m);
			continue;
		}
		mbuf_copydata(m, 0, len, (void *)(ud.dma + UD_TX));
		mbuf_freem(m);
		/* A frame of a multiple of 512 bytes would need a zero-length
		 * packet to end it; one pad byte does the same (IP ignores it). */
		if ((len % 512) == 0) {
			((uint8_t *)(ud.dma + UD_TX))[len++] = 0;
		}
		ud.tx_busy = true;
		ud.tx_frames++;
		ud_start_trb(EP_TX, UD_TRB_TX, UD_TX, (uint32_t)len, TRBCTL_NORMAL);
	}
}

static errno_t
ud_if_output(ifnet_t ifp, mbuf_t m)
{
	lck_mtx_lock(ud.lock);
	if (ud.txq_count == UD_TXQ || !ud.configured) {
		lck_mtx_unlock(ud.lock);
		mbuf_freem(m);
		ifnet_stat_increment_out(ifp, 0, 0, 1);
		return 0;
	}
	ifnet_stat_increment_out(ifp, 1, mbuf_pkthdr_len(m), 0);
	ud.txq[(ud.txq_head + ud.txq_count) % UD_TXQ] = m;
	ud.txq_count++;
	ud_tx_kick_locked();
	lck_mtx_unlock(ud.lock);
	return 0;
}

/*
 * Link state for configd (KernelEventMonitor reads SIOCGIFMEDIA and the
 * KEV_DL_LINK_ON/OFF events; IPMonitor does not route through a service
 * whose interface has no active link).
 */
static void
ud_link_event(bool up)
{
	if (ud.ifp != NULL) {
		dlil_post_msg(ud.ifp, KEV_DL_SUBCLASS, up ? KEV_DL_LINK_ON : KEV_DL_LINK_OFF, NULL, 0, FALSE);
	}
}

static errno_t
ud_if_ioctl(ifnet_t ifp, unsigned long cmd, void *data)
{
	switch (cmd) {
	case SIOCGIFMEDIA32:
	case SIOCGIFMEDIA64: {
		struct ifmediareq *ifmr = (struct ifmediareq *)data;
		user_addr_t list = (cmd == SIOCGIFMEDIA64) ?
		    ((struct ifmediareq64 *)ifmr)->ifmu_ulist :
		    CAST_USER_ADDR_T(((struct ifmediareq32 *)ifmr)->ifmu_ulist);
		int media = IFM_ETHER | IFM_AUTO;
		ifmr->ifm_current = media;
		ifmr->ifm_active = ud.configured ? (IFM_ETHER | IFM_1000_T | IFM_FDX) : media;
		ifmr->ifm_mask = 0;
		ifmr->ifm_status = IFM_AVALID | (ud.configured ? IFM_ACTIVE : 0);
		if (list != USER_ADDR_NULL && ifmr->ifm_count > 0) {
			ifmr->ifm_count = 1;
			return copyout(&media, list, sizeof(media));
		}
		ifmr->ifm_count = 1;
		return 0;
	}
	case SIOCSIFADDR:
	case SIOCAIFADDR:
		ifnet_set_flags(ifp, IFF_UP, IFF_UP);
		return 0;
	case SIOCSIFFLAGS:
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static void
ud_if_detach(ifnet_t ifp __unused)
{
}

/* ue0, 192.168.7.2/24 (the host side is 192.168.7.1). */
static void
ud_if_create(void)
{
	struct ifnet_init_eparams init;
	static lck_grp_t *grp;

	grp = lck_grp_alloc_init("sweet_usb", LCK_GRP_ATTR_NULL);
	ud.lock = lck_mtx_alloc_init(grp, LCK_ATTR_NULL);

	bzero(&init, sizeof(init));
	init.ver = IFNET_INIT_CURRENT_VERSION;
	init.len = sizeof(init);
	init.flags = IFNET_INIT_LEGACY;
	init.uniqueid = "sweet-usb-ecm";
	init.uniqueid_len = sizeof("sweet-usb-ecm") - 1;
	init.name = "ue";
	init.unit = 0;
	init.family = IFNET_FAMILY_ETHERNET;
	init.type = IFT_ETHER;
	init.output = ud_if_output;
	init.demux = ether_demux;
	init.add_proto = ether_add_proto;
	init.del_proto = ether_del_proto;
	init.check_multi = ether_check_multi;
	init.framer_extended = ether_frameout_extended;
	init.ioctl = ud_if_ioctl;
	init.detach = ud_if_detach;
	init.broadcast_addr = etherbroadcastaddr;
	init.broadcast_len = ETHER_ADDR_LEN;
	if (ifnet_allocate_extended(&init, &ud.ifp) != 0) {
		printf("SWEET USB: ifnet_allocate failed\n");
		ud.ifp = NULL;
		return;
	}
	ifnet_set_addrlen(ud.ifp, ETHER_ADDR_LEN);
	ifnet_set_hdrlen(ud.ifp, sizeof(struct ether_header));
	ifnet_set_mtu(ud.ifp, ETHERMTU);
	ifnet_set_baudrate(ud.ifp, 480000000);
	ifnet_set_flags(ud.ifp, IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST, 0xffff);
	if (ifnet_attach(ud.ifp, NULL) != 0) {
		printf("SWEET USB: ifnet_attach failed\n");
		ud.ifp = NULL;
		return;
	}
	ifnet_set_lladdr(ud.ifp, ud_phone_mac, ETHER_ADDR_LEN);

	errno_t err = proto_plumb(PF_INET, ud.ifp);
	socket_t so = NULL;
	if (err == 0) {
		err = sock_socket(PF_INET, SOCK_DGRAM, 0, NULL, NULL, &so);
	}
	if (err == 0) {
		struct ifaliasreq ifra;
		bzero(&ifra, sizeof(ifra));
		snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "ue0");
		struct sockaddr_in *a = (struct sockaddr_in *)&ifra.ifra_addr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xc0a80702);         /* 192.168.7.2 */
		a = (struct sockaddr_in *)&ifra.ifra_mask;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xffffff00);
		a = (struct sockaddr_in *)&ifra.ifra_broadaddr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xc0a807ff);
		err = sock_ioctl(so, SIOCAIFADDR, &ifra);
		sock_close(so);
	}
	ifnet_set_flags(ud.ifp, IFF_UP | IFF_RUNNING, IFF_UP | IFF_RUNNING);
	printf("SWEET USB: ue0 192.168.7.2/24 %s (err %d)\n", err ? "NOT configured" : "up", err);
}

static void
ud_event(uint32_t ev)
{
	if (ev & 1) {                   /* device event */
		uint32_t type = (ev >> 8) & 0xf;
		if (type == 1) {        /* USB reset */
			dwc_wr(DCFG, dwc_rd(DCFG) & ~(0x7fu << 3));
			printf("SWEET USB: bus reset\n");
		} else if (type == 2) { /* connect done */
			uint32_t speed = dwc_rd(DSTS) & 7;
			/* ep0 max packet: 64 at high/full speed (modify action) */
			ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (0u << 25), 0);
			ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (64u << 3) | (2u << 30),
			    (1u << 8) | (1u << 10) | (1u << 25), 0);
			printf("SWEET USB: connected, speed %u\n", speed);
		} else if (type == 0) {
			printf("SWEET USB: disconnect\n");
		}
		return;
	}
	unsigned ep = (ev >> 1) & 0x1f;
	unsigned type = (ev >> 6) & 0xf;
	unsigned status = (ev >> 12) & 0xf;

	if (ep == EP_RX && type == 1) {
		ud_rx_done();
		return;
	}
	if (ep == EP_TX && type == 1) {
		lck_mtx_lock(ud.lock);
		ud.tx_busy = false;
		ud_tx_kick_locked();
		lck_mtx_unlock(ud.lock);
		return;
	}
	if (ep > 1) {
		return;
	}
	/*
	 * Control transfer state machine (Linux dwc3 ep0.c): SETUP completes ->
	 * request handled; the data stage (if any) is started right away; the
	 * status stage is started when the controller reports XferNotReady
	 * with status "control status"; its completion re-arms SETUP.
	 */
	if (type == 1) {                /* XferComplete */
		switch (ud.state) {
		case EP0_SETUP:
			ud_sync(UD_SETUP, 8);
			ud_setup();
			break;
		case EP0_DATA:
			ud.state = EP0_WAIT_STATUS3;
			break;
		case EP0_STATUS:
			ud_start_setup();
			break;
		default:
			break;
		}
		return;
	}
	if (type == 3 && status == 2) { /* XferNotReady, control status phase */
		if (ud.state == EP0_WAIT_STATUS2 || ud.state == EP0_WAIT_STATUS3) {
			uint32_t ctl = ud.state == EP0_WAIT_STATUS3 ? TRBCTL_STATUS3 : TRBCTL_STATUS2;
			ud.state = EP0_STATUS;
			ud_start(ep, UD_DATA, 0, ctl);
		}
	}
}

static void
ud_thread(void *arg __unused, wait_result_t wr __unused)
{
	uint32_t ticks = 0;
	for (;;) {
		uint32_t count = dwc_rd(GEVNTCOUNT) & 0xffff;
		if (count) {
			ud_sync(UD_EVT, UD_EVT_SIZE);
			for (uint32_t done = 0; done < count; done += 4) {
				uint32_t ev = *(volatile uint32_t *)(ud.dma + UD_EVT + ud.evt_pos);
				ud.evt_pos = (ud.evt_pos + 4) % UD_EVT_SIZE;
				if (ud.events++ < 200) {
					printf("SWEET USB: event 0x%08x\n", ev);
				}
				ud_event(ev);
			}
			dwc_wr(GEVNTCOUNT, count);
		}
		if ((++ticks % 30000) == 0) {
			printf("SWEET USB: ue0 rx %llu tx %llu frames\n", ud.rx_frames, ud.tx_frames);
		}
		ud_sleep_ms(1);
	}
}

/*
 * Live console log over USB Ethernet: every 100 ms the new bytes of the
 * console ring (bsd/dev/sweet_ufs.c) go as UDP datagrams to
 * 192.168.7.1:5140 (on the PC: nc -ukl 5140). Starts with the last 64 KiB
 * so the boot is included. Nothing here may print (it would feed itself).
 * Boot-arg sweetusblog=0 disables it.
 */
extern uint32_t sweet_log_copy(uint64_t *pos, char *out, uint32_t max);
extern uint64_t sweet_log_position(void);

static void
ud_log_thread(void *arg __unused, wait_result_t wr __unused)
{
	static char chunk[1400];
	socket_t so = NULL;
	struct sockaddr_in to;
	uint64_t pos = sweet_log_position();

	pos = pos > 65536 ? pos - 65536 : 0;
	bzero(&to, sizeof(to));
	to.sin_len = sizeof(to);
	to.sin_family = AF_INET;
	to.sin_port = htons(5140);
	to.sin_addr.s_addr = htonl(0xc0a80701);         /* 192.168.7.1 */
	for (;;) {
		ud_sleep_ms(100);
		if (!ud.configured) {
			continue;
		}
		if (so == NULL && sock_socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, NULL, &so) != 0) {
			so = NULL;
			continue;
		}
		uint32_t n;
		while ((n = sweet_log_copy(&pos, chunk, sizeof(chunk))) > 0) {
			struct iovec iov = { .iov_base = chunk, .iov_len = n };
			struct msghdr msg = {
				.msg_name = &to, .msg_namelen = sizeof(to),
				.msg_iov = &iov, .msg_iovlen = 1,
			};
			size_t sent = 0;
			if (sock_send(so, &msg, 0, &sent) != 0) {
				break;          /* no route/ARP yet: retry later */
			}
		}
	}
}

static void
sweet_usb_gadget_start(vm_offset_t dwc)
{
	thread_t thread;

	ud.dwc = dwc;
	if (kmem_alloc_contig(kernel_map, &ud.dma, UD_SIZE, PAGE_MASK, 0, 0,
	    KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT) != KERN_SUCCESS) {
		printf("SWEET USB: no DMA memory\n");
		return;
	}
	ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)ud.dma);
	if (!pn) {
		return;
	}
	ud.dma_pa = ptoa_64(pn);
	ud_if_create();

	/* Device controller soft reset (PHY and global setup stay as ABL left them). */
	dwc_wr(DCTL, DCTL_CSFTRST);
	int i;
	for (i = 0; i < 500 && (dwc_rd(DCTL) & DCTL_CSFTRST); i++) {
		delay(1000);
	}
	printf("SWEET USB: soft reset %s after %d ms, GCTL 0x%08x DCFG 0x%08x\n",
	    (dwc_rd(DCTL) & DCTL_CSFTRST) ? "STUCK" : "done", i, dwc_rd(GCTL), dwc_rd(DCFG));
	if (dwc_rd(DCTL) & DCTL_CSFTRST) {
		return;
	}

	dwc_wr(GEVNTADRLO, (uint32_t)(ud.dma_pa + UD_EVT));
	dwc_wr(GEVNTADRHI, (uint32_t)((ud.dma_pa + UD_EVT) >> 32));
	dwc_wr(GEVNTSIZ, (1u << 31) | UD_EVT_SIZE);     /* interrupt masked: polled */
	dwc_wr(GEVNTCOUNT, dwc_rd(GEVNTCOUNT) & 0xffff);
	ud.evt_pos = 0;

	/* High speed, address 0 */
	uint32_t dcfg = dwc_rd(DCFG) & ~(7u | (0x7fu << 3));
	dwc_wr(DCFG, dcfg | 0);
	/* Events: disconnect, USB reset, connect done */
	dwc_wr(DEVTEN, 0x7);

	/* Endpoint 0 OUT/IN: start config, control type, max packet 512 until connect done. */
	if (ud_epcmd(0, DEPCMD_DEPSTARTCFG, 0, 0, 0) ||
	    ud_epcmd(0, DEPCMD_SETEPCONFIG, (0u << 1) | (512u << 3), (1u << 8) | (1u << 10) | (0u << 25), 0) ||
	    ud_epcmd(0, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0) ||
	    ud_epcmd(1, DEPCMD_SETEPCONFIG, (0u << 1) | (512u << 3), (1u << 8) | (1u << 10) | (1u << 25), 0) ||
	    ud_epcmd(1, DEPCMD_SETTRANSFRESOURCE, 1, 0, 0)) {
		printf("SWEET USB: endpoint 0 setup failed\n");
		return;
	}
	dwc_wr(DALEPENA, 0x3);
	ud_start_setup();

	vm_offset_t qs = ml_io_map(QSCRATCH_PA, 0x100);
	if (qs != 0) {
		uint32_t hs = usb_rd(qs, QSCRATCH_HS_PHY_CTRL), ss = usb_rd(qs, QSCRATCH_SS_PHY_CTRL);
		printf("SWEET USB: QSCRATCH HS_PHY_CTRL 0x%08x SS_PHY_CTRL 0x%08x GENERAL_CFG 0x%08x\n",
		    hs, ss, usb_rd(qs, 0x08));
		*(volatile uint32_t *)(qs + QSCRATCH_HS_PHY_CTRL) = hs | UTMI_OTG_VBUS_VALID | SW_SESSVLD_SEL;
		*(volatile uint32_t *)(qs + QSCRATCH_SS_PHY_CTRL) = ss | LANE0_PWR_PRESENT;
		__asm__ volatile ("dsb sy" ::: "memory");
		printf("SWEET USB: VBUS override on: HS_PHY_CTRL 0x%08x SS_PHY_CTRL 0x%08x\n",
		    usb_rd(qs, QSCRATCH_HS_PHY_CTRL), usb_rd(qs, QSCRATCH_SS_PHY_CTRL));
	}

	dwc_wr(DCTL, dwc_rd(DCTL) | DCTL_RUN_STOP);
	for (i = 0; i < 500 && (dwc_rd(DSTS) & DSTS_DEVCTRLHLT); i++) {
		delay(1000);
	}
	printf("SWEET USB: running (DSTS 0x%08x after %d ms), DMA at 0x%llx\n", dwc_rd(DSTS), i, ud.dma_pa);
	if (kernel_thread_start(ud_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
	int livelog = 1;
	PE_parse_boot_argn("sweetusblog", &livelog, sizeof(livelog));
	if (livelog && ud.ifp != NULL && kernel_thread_start(ud_log_thread, NULL, &thread) == KERN_SUCCESS) {
		thread_deallocate(thread);
	}
}
#endif /* NORD2 */
#endif /* ARM64_BOARD_CONFIG_SWEET */
