/* SPDX-License-Identifier: MIT
 * gnss_pwr.h -- MT6893 CONNAC2 GNSS (BGF GPS) power-on core for the Nord2 XNU port (patch 0350, milestone 1).
 *
 * A host-testable, callback-driven transcription of Android's gps_dl power path for MT6893
 * (nord2-current-kernel drivers/misc/mediatek/connectivity/gps/data_link, plat/v010 = "SUPPORT:MT6885,MT6893"):
 *   gps_dl_hw_gps_common_on()  (hw/gps_dl_hw_power_ctrl.c:182) with the plat/v010 dep functions, and the two steps
 *   the conninfra driver does for GPS on MT6893 (conninfra/platform/mt6893/mt6893_pos.c: BGFSYS_ON_TOP power with
 *   key 0x4254 at 0x18000008, and consys_adie_top_ck_en_ctrl: 0x18001900[1] + poll 0x18005008[26] == 0).
 * Register addresses/masks below were resolved from plat/v010/hw/inc by compiling them on the host (see GNSS_KERNEL.md).
 *
 * Preconditions (NOT done here, already done by our Wi-Fi bring-up when it runs): conninfra powered and awake
 * (VCN18/VCN13 rails, conn_infra on, HOST1a0 wake vote), A-die initialised, EMI remap window for GPS DMA buffers known.
 */
#ifndef N2G_GNSS_PWR_H
#define N2G_GNSS_PWR_H
#ifndef N2G_KERNEL
#include <stdint.h>
#endif
#ifndef N2G_API
#define N2G_API            /* the kernel include defines it as static __attribute__((unused)) */
#endif

/* conn_infra (AP physical == conn_infra bus address) */
#define N2G_CONN_HW_VER            0x18001000u   /* RO, full word */
#define N2G_CONN_HW_VER_MT6893     0x20010101u
#define N2G_CONN_HW_VER_MT6885     0x20010000u
#define N2G_RGU_BGFSYS_PWR_CTL     0x18000008u   /* [31:16] key 0x4254, [7] BGFSYS_ON_TOP_PWR_ON */
#define N2G_RGU_BGFSYS_PWR_KEY     0x42540000u
#define N2G_RGU_BGFSYS_PWR_ACK     0x18000114u   /* [24] primary ack, [25] secondary ack */
#define N2G_CFG_GPS_PWRCTRL0       0x18001878u   /* [0] GP_FUNCTION_EN */
#define N2G_CFG_EMI_CTL            0x18001c1cu   /* [0] GPS_EMI_REQ_GPS */
#define N2G_CFG_GALS_CONN2GPS      0x18001618u   /* [0] TX_EN [3] TX_RDY [4] RX_EN [7] RX_RDY (sleep protection) */
#define N2G_CFG_GALS_GPS2CONN      0x1800161cu   /* same layout */
#define N2G_CFG_ADIE_CTL           0x18001900u   /* [1] A-die top clock enable (conninfra, host GPS vote) */
#define N2G_WTSLP_WB_STA           0x18005008u   /* [26] busy: poll == 0 */
#define N2G_CSR_CONN2AP_GPS_EMI    0x180601d0u   /* [19:0] GPS EMI remap base (phys >> 16) */
#define N2G_CSR_HOST2GPS_DBG_SEL   0x180600b0u   /* [7:0] */
#define N2G_CSR_GPS_CFG2HOST_DBG   0x18060240u   /* [15:0]; with sel 0x80: [3:0] == 2 means gps_top_off GPS_ACTIVE */
#define N2G_CSR_WAKEPU_GPS         0x180601acu   /* [0] GPS vote: wake conn_infra top_off (gps_dl_hw_gps_force_wakeup_conninfra_top_off) */
#define N2G_CSR_SLP_PROT_CTRL      0x18060184u   /* [5] CONN_INFRA_ON2OFF_SLP_PROT_ACK: poll == 0 (up to 10 ms) after the wake vote */
/* GPS bus 0x80000000.. is seen by the AP at 0x18C00000 (gps_dl_hw_gps.c g_gps_addr_table: {0x18C00000, 0x80000000, 0x90000}) */
#define N2G_GPS_BUS(a)             (0x18C00000u + ((a) - 0x80000000u))
#define N2G_GPS_CLKGEN1_CTL        N2G_GPS_BUS(0x8000103cu)  /* [4] CR_GPS_DIGCK_DIV_EN (PLL driver) */
#define N2G_GPS_AON_DSLEEP_CTL     N2G_GPS_BUS(0x80073108u)  /* [1:0] GPS_PWR_STAT [3] FORCE_OSC_EN_ON [4] DIS_HW_RST_CNT */

#define N2G_POLL_MAX   1000   /* polls of 1 us-ish each, the vendor POLL_DEFAULT is 1000 x 1 us */

struct n2g_hw {
	void *ctx;
	uint32_t (*read32)(void *ctx, uint32_t pa);
	void (*write32)(void *ctx, uint32_t pa, uint32_t val);
	void (*delay_us)(void *ctx, unsigned us);
	void (*log)(void *ctx, const char *step, uint32_t a, uint32_t b);
	uint32_t emi_base_pa;    /* GPS DMA buffers (L1/L5 TX/RX), 64 KiB aligned, inside the remap window */
};

enum n2g_err {
	N2G_OK = 0,
	N2G_E_HWVER = 1,        /* conn_infra version is neither MT6893 nor MT6885: nothing written */
	N2G_E_BGF_ACK1 = 2,     /* BGFSYS_ON_TOP primary power ack */
	N2G_E_BGF_ACK2 = 3,     /* secondary ack */
	N2G_E_SLP_PROT = 4,     /* a sleep-protection RDY did not clear */
	N2G_E_GPS_TOP = 5,      /* gps_top_off never reached GPS_ACTIVE (debug readout != 2) */
	N2G_E_ADIE_CLK = 6,     /* A-die top clock WB_STA busy did not clear */
	N2G_E_ARG = 7,
	N2G_E_TIMEOUT = 9,      /* data link: the DSP ROM okay flag (USRT D2AF3) never came */
	N2G_E_WAKE = 8,         /* conn_infra did not wake for the GPS vote (on2off sleep protection ack stayed set) */
};

/* Power the GNSS block on (gps_dl_hw_gps_common_on order). On failure undoes what the vendor undoes
 * (GP_FUNCTION_EN = 0, GPS_EMI_REQ = 0) and returns the error. */
N2G_API int n2g_common_on(const struct n2g_hw *hw);
/* L1 DSP on/off per gps_dl_hw_gps_pwr_stat_ctrl (GPS_L1_DSP_ON: PWR_STAT=0, FORCE_OSC=0, DIS_HW_RST_CNT pulse). */
N2G_API int n2g_dsp_on(const struct n2g_hw *hw);
/* gps_dl_hal_link_power_ctrl(GPS_DL_HAL_POWER_ON) for L1: wake vote, common_on, L1 DSP on, wake vote released
 * (the conninfra bus-clock WPLL/BPLL hold the vendor adds around it is done by conninfra on Linux; on our port the
 * Wi-Fi owner keeps conn_infra clocked: inferred). */
N2G_API int n2g_link_on(const struct n2g_hw *hw);
/* Reverse of common_on (subset of gps_dl_hw_gps_common_off: sleep protection on, function off, EMI req off, clocks off). */
N2G_API int n2g_common_off(const struct n2g_hw *hw);
/* 1 when pa is one of the registers this core may touch (the binding maps only these pages). */
N2G_API int n2g_reg_allowed(uint32_t pa);

#endif
