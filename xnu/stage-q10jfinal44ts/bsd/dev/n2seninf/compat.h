/* Linux shims for MediaTek's ISP6s seninf hardware layer (GPL-2.0, TCLOpenSource/mt9653
 * drivers/media/platform/mtk-isp/isp_60s/cam/mtk_cam-seninf-hw.c) in XNU. */
#ifndef N2SENINF_COMPAT_H
#define N2SENINF_COMPAT_H
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64; typedef int64_t s64;
#define __iomem
#define udelay(us) delay(us)
#define usleep_range(a, b) delay(a)
#define mdelay(ms) delay((ms) * 1000)
#define dev_info(d, ...) printf("NORD2 SENINF: " __VA_ARGS__)
#define dev_err(d, ...) printf("NORD2 SENINF: ERR " __VA_ARGS__)
#define dev_dbg(d, ...) do { } while (0)
#define do_div(n, d) ((n) = (n) / (d))
#ifndef EINVAL
#define EINVAL 22
#endif
#include "mtk_cam-seninf-def.h"
struct device;
struct seninf_vc { u8 vc, dt, feature, out_pad, pixel_mode, group, mux, cam, enable; u16 exp_hsize, exp_vsize; };
struct seninf_vcinfo { struct seninf_vc vc[SENINF_VC_MAXCNT]; int cnt; };
struct seninf_ctx {
	struct { const char *name; } subdev;
	struct device *dev;
	int pads[PAD_MAXCNT];
	int port, portNum, portA, portB, num_data_lanes;
	s64 mipi_pixel_rate, buffered_pixel_rate;
	unsigned int is_4d1c:1, is_cphy:1, is_test_model:1, is_test_streamon:1;
	int seninfIdx;
	int pad2cam[PAD_MAXCNT];
	struct seninf_vcinfo vcinfo;
	int fps_n, fps_d;
	int isp_freq;
	void *reg_ana_csi_rx[CSI_PORT_MAX_NUM];
	void *reg_ana_dphy_top[CSI_PORT_MAX_NUM];
	void *reg_ana_cphy_top[CSI_PORT_MAX_NUM];
	void *reg_if_top;
	void *reg_if_ctrl[SENINF_NUM];
	void *reg_if_cam_mux;
	void *reg_if_tg[SENINF_NUM];
	void *reg_if_csi2[SENINF_NUM];
	void *reg_if_mux[SENINF_MUX_NUM];
	unsigned int streaming:1;
};
#endif
