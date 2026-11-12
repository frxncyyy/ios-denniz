/* SPDX-License-Identifier: MIT
 * gnss_pwr.c -- see gnss_pwr.h. Source order of gps_dl_hw_gps_common_on() for MT6893 (plat/v010), with the conninfra
 * driver's GPS steps inlined where the vendor delegates to it (marked [conninfra]). Every register access goes through
 * the callbacks, every address is in n2g_reg_allowed().
 */
#ifndef N2G_KERNEL
#include "gnss_pwr.h"
#endif

static const uint32_t n2g_regs[] = {
	N2G_CONN_HW_VER, N2G_RGU_BGFSYS_PWR_CTL, N2G_RGU_BGFSYS_PWR_ACK, N2G_CFG_GPS_PWRCTRL0, N2G_CFG_EMI_CTL,
	N2G_CFG_GALS_CONN2GPS, N2G_CFG_GALS_GPS2CONN, N2G_CFG_ADIE_CTL, N2G_WTSLP_WB_STA, N2G_CSR_CONN2AP_GPS_EMI,
	N2G_CSR_HOST2GPS_DBG_SEL, N2G_CSR_GPS_CFG2HOST_DBG, N2G_CSR_WAKEPU_GPS, N2G_CSR_SLP_PROT_CTRL, N2G_GPS_CLKGEN1_CTL, N2G_GPS_AON_DSLEEP_CTL,
	/* 0357: GPS RGU_ON L1 control/delay (bus 0x80000014/160/164) and CFG_ON L1 sleep-power state (0x80001020) */
	0x18c00014u, 0x18c00160u, 0x18c00164u, 0x18c01020u,
};

N2G_API int n2g_reg_allowed(uint32_t pa)
{
	for (unsigned i = 0; i < sizeof n2g_regs / sizeof n2g_regs[0]; i++)
		if (n2g_regs[i] == pa) return 1;
	/* M2 (gnss_dl.c): BGF_GPS_DMA channels 1..4 (bus 0x80010108..0x80010448) and the two USRT APB blocks */
	if (pa >= 0x18c10108u && pa < 0x18c10500u) {
		uint32_t o = (pa - 0x18c10000u) & 0xffu;
		if (pa >= 0x18c10100u && ((pa - 0x18c10000u) >> 8) <= 4u &&
		    (o == 0x08 || o == 0x0c || o == 0x10 || o == 0x14 || o == 0x18 || o == 0x1c || o == 0x20 || o == 0x24 || o == 0x2c || o == 0x48))
			return 1;
	}
	if ((pa & ~0x3fu) == 0x18c73000u || (pa & ~0x3fu) == 0x18c83000u) {
		uint32_t o = pa & 0x3fu;
		if (o == 0x04 || o == 0x08 || o == 0x0c || o == 0x20 || o == 0x24 || o == 0x34) return 1;
	}
	return 0;
}

static uint32_t n2g_rd(const struct n2g_hw *hw, uint32_t pa) { return hw->read32(hw->ctx, pa); }
static void n2g_wr(const struct n2g_hw *hw, uint32_t pa, uint32_t v) { hw->write32(hw->ctx, pa, v); }
static void n2g_field(const struct n2g_hw *hw, uint32_t pa, uint32_t mask, uint32_t val)      /* GDL_HW_SET_*_ENTRY */
{
	n2g_wr(hw, pa, (n2g_rd(hw, pa) & ~mask) | (val & mask));
}
static int n2g_poll(const struct n2g_hw *hw, uint32_t pa, uint32_t mask, uint32_t want)       /* GDL_HW_POLL_*_ENTRY */
{
	for (int i = 0; i < N2G_POLL_MAX; i++) {
		if ((n2g_rd(hw, pa) & mask) == want) return 1;
		hw->delay_us(hw->ctx, 1);
	}
	return 0;
}
static void n2g_note(const struct n2g_hw *hw, const char *s, uint32_t a, uint32_t b) { if (hw->log) hw->log(hw->ctx, s, a, b); }

/* gps_dl_hw_gps_sleep_prot_ctrl(1): disable the four GALS sleep protections, each EN then wait RDY == 0 */
static int n2g_slp_prot_off(const struct n2g_hw *hw)
{
	static const struct { uint32_t pa, en, rdy; } s[4] = {
		{ N2G_CFG_GALS_CONN2GPS, 1u << 4, 1u << 7 },   /* conn2gps rx */
		{ N2G_CFG_GALS_CONN2GPS, 1u << 0, 1u << 3 },   /* conn2gps tx */
		{ N2G_CFG_GALS_GPS2CONN, 1u << 4, 1u << 7 },   /* gps2conn rx */
		{ N2G_CFG_GALS_GPS2CONN, 1u << 0, 1u << 3 },   /* gps2conn tx */
	};
	for (int i = 0; i < 4; i++) {
		n2g_field(hw, s[i].pa, s[i].en, 0);
		if (!n2g_poll(hw, s[i].pa, s[i].rdy, 0)) { n2g_note(hw, "slp_prot_off_timeout", s[i].pa, (uint32_t)i); return -1; }
	}
	return 0;
}

/* sleep_prot_ctrl(0): enable again, vendor order conn2gps tx, conn2gps rx, gps2conn tx, gps2conn rx */
static int n2g_slp_prot_on(const struct n2g_hw *hw)
{
	static const struct { uint32_t pa, en, rdy; } s[4] = {
		{ N2G_CFG_GALS_CONN2GPS, 1u << 0, 1u << 3 },
		{ N2G_CFG_GALS_CONN2GPS, 1u << 4, 1u << 7 },
		{ N2G_CFG_GALS_GPS2CONN, 1u << 0, 1u << 3 },
		{ N2G_CFG_GALS_GPS2CONN, 1u << 4, 1u << 7 },
	};
	int rc = 0;
	for (int i = 0; i < 4; i++) {
		n2g_field(hw, s[i].pa, s[i].en, s[i].en);
		if (!n2g_poll(hw, s[i].pa, s[i].rdy, s[i].rdy)) { n2g_note(hw, "slp_prot_on_timeout", s[i].pa, (uint32_t)i); rc = -1; }
	}
	return rc;
}

N2G_API int n2g_common_on(const struct n2g_hw *hw)
{
	if (!hw || !hw->read32 || !hw->write32 || !hw->delay_us || (hw->emi_base_pa & 0xffffu)) return N2G_E_ARG;
	hw->delay_us(hw->ctx, 200);                                   /* wake up 3T 32k clock to ready */
	uint32_t ver = 0;
	int ok = 0;
	for (int i = 0; i < N2G_POLL_MAX && !ok; i++) {                /* GDL_HW_CHECK_CONN_INFRA_VER */
		ver = n2g_rd(hw, N2G_CONN_HW_VER);
		ok = ver == N2G_CONN_HW_VER_MT6893 || ver == N2G_CONN_HW_VER_MT6885;
		if (!ok) hw->delay_us(hw->ctx, 1);
	}
	if (!ok) { n2g_note(hw, "hw_ver_bad", ver, 0); return N2G_E_HWVER; }
	n2g_note(hw, "hw_ver", ver, 0);
	/* v010: restore-done check and conn2ap peri remap are no-ops for MT6885/MT6893 */
	n2g_field(hw, N2G_CSR_CONN2AP_GPS_EMI, 0x000fffffu, hw->emi_base_pa >> 16);      /* gps_dl_emi_remap_set */
	/* [conninfra] GDL_HW_SET_CONN_INFRA_BGF_EN: mt6893_pos.c writes key 0x4254 | bit7 under mask 0xffff0080 */
	n2g_field(hw, N2G_RGU_BGFSYS_PWR_CTL, 0xffff0080u, N2G_RGU_BGFSYS_PWR_KEY | (1u << 7));
	/* EMI request is deferred until data routing on Linux (gps_dl_hal_emi_usage_init); peri request: no-op on v010 */
	n2g_field(hw, N2G_CFG_GPS_PWRCTRL0, 1u, 1u);                                     /* GDL_HW_SET_GPS_FUNC_EN(1) */
	int err = 0;
	if (!n2g_poll(hw, N2G_RGU_BGFSYS_PWR_ACK, 1u << 24, 1u << 24)) err = N2G_E_BGF_ACK1;
	else if (!n2g_poll(hw, N2G_RGU_BGFSYS_PWR_ACK, 1u << 25, 1u << 25)) err = N2G_E_BGF_ACK2;
	if (!err) {
		hw->delay_us(hw->ctx, 200);
		if (n2g_slp_prot_off(hw)) err = N2G_E_SLP_PROT;
	}
	if (!err) {                                                   /* gps_dl_hw_dep_poll_bgf_bus_and_gps_top_ack */
		n2g_field(hw, N2G_CSR_HOST2GPS_DBG_SEL, 0xffu, 0x80u);
		int top = 0;
		for (int i = 0; i < 3 && !top; i++) top = n2g_poll(hw, N2G_CSR_GPS_CFG2HOST_DBG, 0xffffu, 2u);
		if (!top) err = N2G_E_GPS_TOP;
	}
	if (!err) {                                                   /* [conninfra] consys_adie_top_ck_en_ctrl(true) */
		n2g_field(hw, N2G_CFG_ADIE_CTL, 1u << 1, 1u << 1);
		if (!n2g_poll(hw, N2G_WTSLP_WB_STA, 1u << 26, 0)) err = N2G_E_ADIE_CLK;
	}
	if (err) {
		n2g_note(hw, "common_on_fail", (uint32_t)err, 0);
		n2g_field(hw, N2G_CFG_GPS_PWRCTRL0, 1u, 0);                    /* GDL_HW_SET_GPS_FUNC_EN(0) */
		n2g_field(hw, N2G_CFG_EMI_CTL, 1u, 0);                         /* GPS_EMI_REQ_GPS = 0 */
		return err;
	}
	n2g_field(hw, N2G_GPS_CLKGEN1_CTL, 1u << 4, 1u << 4);              /* PLL driver; BPLL: no-op on MT6893 */
	n2g_note(hw, "common_on_ok", ver, hw->emi_base_pa);
	return N2G_OK;
}

N2G_API int n2g_dsp_on(const struct n2g_hw *hw)
{
	/* gps_dl_hw_gps_pwr_stat_ctrl(GPS_L1_DSP_ON): PWR_STAT = 0, FORCE_OSC_EN_ON = 0, DIS_HW_RST_CNT 1 then 0 */
	n2g_field(hw, N2G_GPS_AON_DSLEEP_CTL, 0x3u, 0);
	n2g_field(hw, N2G_GPS_AON_DSLEEP_CTL, 1u << 3, 0);
	n2g_field(hw, N2G_GPS_AON_DSLEEP_CTL, 1u << 4, 1u << 4);
	n2g_field(hw, N2G_GPS_AON_DSLEEP_CTL, 1u << 4, 0);
	return N2G_OK;
}

N2G_API int n2g_link_on(const struct n2g_hw *hw)
{
	if (!hw || !hw->read32 || !hw->write32 || !hw->delay_us) return N2G_E_ARG;
	n2g_field(hw, N2G_CSR_WAKEPU_GPS, 1u, 1u);
	int ok = 0;
	for (int i = 0; i < 10 * N2G_POLL_MAX && !ok; i++) {          /* GDL_HW_MAY_WAIT_CONN_INFRA_SLP_PROT_DISABLE_ACK */
		ok = (n2g_rd(hw, N2G_CSR_SLP_PROT_CTRL) & (1u << 5)) == 0;
		if (!ok) hw->delay_us(hw->ctx, 1);
	}
	int rc = ok ? n2g_common_on(hw) : N2G_E_WAKE;
	if (rc == N2G_OK) rc = n2g_dsp_on(hw);
	n2g_field(hw, N2G_CSR_WAKEPU_GPS, 1u, 0);
	return rc;
}

N2G_API int n2g_common_off(const struct n2g_hw *hw)
{
	int rc = n2g_slp_prot_on(hw) ? N2G_E_SLP_PROT : N2G_OK;
	n2g_field(hw, N2G_GPS_CLKGEN1_CTL, 1u << 4, 0);
	n2g_field(hw, N2G_CFG_ADIE_CTL, 1u << 1, 0);                       /* [conninfra] top clock vote off */
	n2g_field(hw, N2G_CFG_GPS_PWRCTRL0, 1u, 0);
	n2g_field(hw, N2G_CFG_EMI_CTL, 1u, 0);
	return rc;
}
