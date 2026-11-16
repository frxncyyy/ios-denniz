/*
 * nord2_bt.h -- Bluetooth for the Nord 2 port (MT6893 / CONNAC2 soc3_0 BGFSYS behind BTIF), pieces 0720..0722.
 *
 *   0720 nord2_bt.c          controller: conninfra wake vote, BT firmware into the CONN EMI carve-out, BGFSYS power-on,
 *                            BTIF (PIO), driver own, WMT_FUNC_CTRL BT on, HCI probe, BD address; sysctls kern.n2bt*
 *   0721 nord2_bt_sk.c       what iOS 16's bluetoothd opens: kernel control com.apple.uart.sk.bluetooth + a Skywalk
 *                            kernel pipe (transport H4BC), /dev/btwake /dev/btreset /dev/btpoweroff, H4 framing, the
 *                            answer rewrites Apple's stack needs from a MediaTek controller
 *   0722 Nord2Bluetooth.cpp  the IORegistry service "bluetooth" (local-mac-address, transport-encoding)
 *
 * The three are one unit (the builder refuses a subset); every behaviour has a run-time switch (sysctl, see nord2_bt.c).
 * Log prefixes: "NORD2 BT0:" controller, "NORD2 BT1:" transport, "NORD2 BT2:" registry, "NORD2 BT U:" the user daemon.
 *
 * The transport (0721) is derived from HakiOS (github.com/oosexgang/hakios-mt6768, kernel/bsd/dev/redwood_btsk.c and
 * redwood_mtkbt_skel.c, (c) 2026 The HakiOS authors, Apple Public Source License 2.0), adapted to this port.
 * The controller sequence (0720) follows MediaTek's GPL-2.0 Linux drivers for MT6893 (MiCode/MTK_kernel_modules,
 * branch ares-r-oss: connectivity/bt/mt66xx/connac2/btmtk_mt66xx.c, conninfra/platform/mt6893/...), read, not copied.
 */
#ifndef NORD2_BT_H
#define NORD2_BT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 0720: the controller (only the bridge thread calls send/receive) ---- */
int      n2bt_hci_send(const uint8_t *h4, unsigned length);                  /* one whole H4 packet; 0 or errno */
int      n2bt_hci_receive(uint8_t *h4, unsigned room, unsigned wait_ms);     /* one whole H4 packet: its length; 0 none; < 0 error */
void     n2bt_housekeeping(void);                                           /* called by the bridge about every second */
uint32_t n2bt_quirks(void);                                                 /* kern.n2bt_quirks */
uint32_t n2bt_trace_left(void);                                             /* consumes one unit of kern.n2bt_trace, 0 = none left */
void     n2bt_set_state(const char *state);                                 /* kern.n2bt_state text */
void     n2bt_bdaddr(uint8_t out[6]);                                       /* the controller's address (as Read_BD_ADDR, LSB first) */
int      n2bt_addr_cmd(uint8_t c[10]);                                      /* bt4: H4 MediaTek set-address command for the address in use (10) or 0 */
void     n2bt_trail(const char *fmt, ...) __printflike(1, 2);              /* bt4: persistent breadcrumb (+ console) */
void     n2bt_rx_debug(uint32_t *have, uint32_t *lsr, uint8_t first[4]);   /* bt6: RX reassembly state for the watchdog */
void     n2bt_now(const char *step, uint32_t a, uint32_t b);                /* bt8: the persistent "what the BT thread does now" slot */
void     n2bt_counters(uint32_t out[6]);
void     n2bt_diag(const char *why);                                        /* bt8: always-on host CSR snapshot (BGF MCU PC x3, LPCTL, IRQ, slp prot) as a breadcrumb */
int      n2bt_restart(const char *why);                                     /* bt8: controller power cycle S5..S9 (0 = answering again) */
uint32_t n2bt_recover_on(void);
void     n2bt_snap(const char *tag);
uint32_t n2bt_reset_requested(void);
void     n2bt_tx_fail(uint32_t out[4]);
uint32_t n2bt_mode_next(uint32_t survived_ms, uint32_t acl_tx, uint32_t acl_rx);   /* bt20 */
extern uint32_t n2bt_tx_mode;                                                /* bt20 */
extern uint32_t n2bt_poll_idle_us;                                           /* bt21 */                                     /* bt19: last TX timeout: bytes written, packet length, LSR, us */
extern uint32_t n2bt_poll_us;
extern uint32_t n2bt_tx_bps;
extern uint32_t n2bt_a2dp_filter;                                            /* bt18: diagnostic, default 0 */                                                 /* bt17 */                                                /* bt11 */                                        /* bt10: the firmware asked for a subsys reset (assert); read-and-clear */                                        /* bt9: always-on registers (votes, LPCTL, MCU PC, sleep prot) as an S11v line */                                             /* bt8: kern.n2bt_recover */                                    /* bt8: rx resync, rx overrun, tx timeouts, max TX wait us, rx drained during TX, own_clr */

/* ---- 0721: the transport; runs the bridge for ever on success, returns an errno on failure ---- */
int      n2bt_transport_run(void);

/* ---- 0722: the registry; address in human order (MSB first, as bluetoothd prints it) ---- */
int      n2bt_registry_publish(const uint8_t addr_msb_first[6]);
void     n2bt_chosen_address(const uint8_t addr_msb_first[6], const char *source);   /* bt6: IODeviceTree:/chosen mac-address-bluetooth0 */
void     n2bt_early_start(void);                                                   /* bt6 (0720): boot-time thread, no hardware access */

/* quirk bits (kern.n2bt_quirks, default N2BT_Q_DEFAULT) */
#define N2BT_Q_VSC_DONE        0x01u   /* Apple/Broadcom vendor commands answered "Unknown command" -> Command Complete (+ the data its parser reads) */
#define N2BT_Q_ERRDATA         0x02u   /* Write_Default_Erroneous_Data_Reporting unknown -> done */
#define N2BT_Q_LE_CMDS         0x04u   /* Read_Local_Supported_Commands: LE octets 24..28 OR-ed in */
#define N2BT_Q_DROP_MTK_EVT    0x08u   /* MediaTek vendor events 0xE4 (WMT) / 0xFF not handed to bluetoothd */
#define N2BT_Q_BCM_BDADDR      0x10u   /* Broadcom Write_BD_ADDR (0xFC01) answered as done without touching the controller */
#define N2BT_Q_READDR          0x20u   /* bt4: the per-phone address set again after every HCI_Reset from bluetoothd (its answer swallowed) */
#define N2BT_Q_LONGLOCAL       0x40u   /* bt6: Write Local Name (0x0c13) / Write EIR (0x0c52) answered in the kernel, not sent */
#define N2BT_Q_SCANFIX         0x80u   /* bt7: Scan Enable(0) before LE Set Scan Parameters; 0x0c (Command Disallowed) on 0x200b/0x200c -> retry once, then status 0 */
#define N2BT_Q_NOEXT           0x100u  /* bt8: LE Read Periodic Advertiser List Size (0x204a, an "extended advertising" command) answered in the kernel */
#define N2BT_Q_LESCAN_LOCAL    0x200u  /* bt8: LE Set Scan Parameters / Scan Enable never sent (answered status 0 in the kernel); default off */
#define N2BT_Q_VSC_PAD         0x400u  /* bt12 (off by default since bt13): blanket >= 16 zero data bytes for every vendor answer (broke 0xfce9 init in bt32a) */
#define N2BT_Q_VSC_TARGET      0x800u  /* bt13: data only where bluetoothd's parser reads fields: 0xfce2 sub 0x0d -> 8, sub 0x2d -> 4, sub 0x05/0x0a -> 1 */
#define N2BT_Q_DEFAULT         0x9ffu

#ifdef __cplusplus
}
#endif
#endif
