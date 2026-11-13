/*
 * Nord 2 "no stock kernel" stub, C part (see head.S). MMU and caches off: every access is Device memory.
 *
 * PMIF SWINF2 (0x10026000) and the MT6359P RTC registers are used exactly as sweet_arm_fastboot() in
 * bsd/kern/sweet_amfi.c does from XNU (patch 0246, used at every boot of the daily kernels):
 *   RTC_PDN1 (0x5b4) |= bit13 RTC_FAST_BOOT, RTC_WRTGR (0x5c2) = 1, wait RTC_BBPU (0x588) CBUSY.
 * LK clears the bit and stops in fastboot. When PMIF does not answer nothing is written to the PMIC and
 * the reset still happens (the phone then boots this image again; hold the fastboot key combination).
 */
#include <stdint.h>

#define PMIF 0x10026000UL
#define PSTORE_BASE 0x48090000UL
#define PSTORE_SIZE 0xe0000UL
#define PMSG_SIZE 0x10000UL
#define PMSG_BASE (PSTORE_BASE + PSTORE_SIZE - PMSG_SIZE)
#define RING_HDR 12UL
#define PMSG_SIG 0x43474244UL

void reset_now(void) __attribute__((noreturn));
void stub_main(void);

static uint32_t rd(uintptr_t a) { return *(volatile uint32_t *)a; }
static void wr(uintptr_t a, uint32_t v) { *(volatile uint32_t *)a = v; }

/* Append to the loader's pmsg ring only when it is the ring the loader/XNU left (valid, not full). */
static void ring_log(const char *s)
{
    volatile uint32_t *hdr = (volatile uint32_t *)PMSG_BASE;      /* sig, start, size */
    volatile char *data = (volatile char *)(PMSG_BASE + RING_HDR);
    if (hdr[0] != PMSG_SIG) {
        return;
    }
    uint32_t n = hdr[2];
    if (n >= PMSG_SIZE - RING_HDR) {
        return;
    }
    while (*s && n < PMSG_SIZE - RING_HDR) {
        data[n++] = *s++;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    hdr[2] = n;
    __asm__ volatile("dsb sy" ::: "memory");
}

static int pw_wait(unsigned fsm)
{
    for (unsigned i = 0; i < 100000; i++) {
        if (((rd(PMIF + 0xca8) >> 1) & 7) == fsm) {
            return 0;
        }
    }
    return -1;
}

static int pw_read(uint32_t addr, uint32_t *val)
{
    if (pw_wait(0)) {
        return -1;
    }
    wr(PMIF + 0xc80, addr);
    if (pw_wait(6)) {
        return -2;
    }
    *val = rd(PMIF + 0xc94) & 0xffff;
    wr(PMIF + 0xca4, 1);
    return 0;
}

static int pw_write(uint32_t addr, uint32_t val)
{
    if (pw_wait(0)) {
        return -1;
    }
    wr(PMIF + 0xc84, val);
    wr(PMIF + 0xc80, (1U << 29) | addr);
    return pw_wait(0) ? -2 : 0;
}

void stub_main(void)
{
    uint32_t v = 0;
    int armed = 0;

    ring_log("NORD2 STUB: no Android kernel in this boot image (NORD2_BOOT_COMPRESS=nostock): reset to LK fastboot\n");
    if (pw_read(0x5b4, &v) == 0) {
        int w1 = pw_write(0x5b4, v | 0x2000);
        int w2 = pw_write(0x5c2, 1);
        for (unsigned i = 0; i < 2000; i++) {       /* rtc_busy_wait: RTC_BBPU bit6 CBUSY */
            uint32_t b = 0;
            if (pw_read(0x588, &b) || !(b & 0x40)) {
                break;
            }
        }
        armed = (w1 == 0 && w2 == 0);
    }
    ring_log(armed ? "NORD2 STUB: RTC fast-boot bit written; TOPRGU reset\n" :
        "NORD2 STUB: PMIF did not answer, fast-boot bit NOT written; TOPRGU reset\n");
    reset_now();
}
