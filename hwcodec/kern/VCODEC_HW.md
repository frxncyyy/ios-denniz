# /dev/redwood-vcodec: the MT6893 VENC / VDEC hardware, as this kernel drives it

Owner: hw-k02 (package K02). Source of the sequences: the OnePlus MT6893 kernel tree `K` =
`$HOME/Documents/Codex/2026-10-19/vedi-questa-repo-https-github-com/work/nord2-current-kernel`
(GPL-2.0), the phone's device tree (`mt6893.dts`) and the four frozen ABI/interface headers in
`gpu/hwcodec/include/`. **Nothing below has been run on the phone.** `[V]` = verified in the source today (path:line given),
`[I]` = inferred from it. The only phone-run sequence of this family is M1 (VEN domain + `venc_sel` + larb7 + JPGENC,
`gpu/hwcodec/runs/q10hw1/`); the VEN half here is the same sequence.

Registers cited without a base are offsets in the block named in the row.

## 0. Address map, windows, IRQ lines

| Block | Power domain | SPM status | Register windows (base + size) | Clock controller | LARB | IRQ |
|---|---|---|---|---|---|---|
| VENC core 0 | VEN | 0x16c/0x170 bit 17 | 0x17020000+0x3000 (`RWVC_WIN_VENC`) | 0x17000000 | larb7 0x17010000 | SPI 308 |
| VENC core 1 | VEN_CORE1 | bit 18 | 0x17820000+0x3000 (`RWVC_WIN_VENC_C1`) | 0x17800000 | larb8 0x17810000 | SPI 313 |
| VDEC core (VDE2) | VDE2 | bit 16 | 0x1602f000, 0x16000000, 0x16020000, 0x16021000, 0x16023000, 0x16025000+0x4000, 0x16004000 | 0x1602f000 | larb4 0x1602e000 | SPI 424 |
| VDEC LAT/SOC (VDE) | VDE | bit 15 | 0x16010000+0x1000, 0x16011000+0x400, 0x1600f000 | 0x1600f000 | larb5 0x1600d000 | SPI 425 |

Evidence: device tree `K/arch/arm64/boot/dts/mediatek/mt6893.dts` (dts from here on): `venc@17000000` :5753 with
`interrupts = <308>` :5761 and regs 0x17020000/0x17820000 :5755-5756; `venc@17820000` :5849 with SPI 313 :5852;
`vdec@16000000` :6281 with the ten windows :6283-6292 and SPIs 424/425 :6297-6298; larb4 :6029 (0x1602e000), larb5 :6040
(0x1600d000), larb7 :6060 (0x17010000), larb8 :6071 (0x17810000); clock controllers 0x17000000/0x17800000 :5738-5744,
0x1600f000 :5881 and 0x1602f000 :5887; SPM/infracfg/topckgen :956-960. The domain number ↔ block mapping is from
`RWVC_BLK_*` in `include/redwood_vcodec.h`: VENC=VENC core 0, VENC_C1=core 1, VDEC_SOC=VDE, VDEC_CORE=VDE2 (same as
`mtk_vcodec_dec_pm.c` hw_id CORE using vdec_gcon/VDE2 and LAT using vdec_soc_gcon/VDE, see §4).

Window → block mapping used for `RWVC_REGS` validation (a window may only be touched while its block is powered):

| `RWVC_WIN_*` | Base | Bytes | Power block |
|---|---|---|---|
| VENC | 0x17020000 | 0x3000 (vpud mmaps 0x3000; JPGENC starts at 0x17030000) | VEN |
| VENC_C1 | 0x17820000 | 0x3000 (same) | VEN_CORE1 |
| VDEC_SYS | 0x1602f000 | 0x1000 | VDEC_CORE (VDE2) |
| VDEC_BASE | 0x16000000 | 0x1000 | VDEC_CORE |
| VDEC_VLD | 0x16020000 | 0x1000 | VDEC_CORE |
| VDEC_MC | 0x16021000 | 0x1000 | VDEC_CORE |
| VDEC_MV | 0x16023000 | 0x1000 | VDEC_CORE |
| VDEC_MISC | 0x16025000 | 0x4000 | VDEC_CORE |
| VDEC_LAT_MISC | 0x16010000 | 0x1000 | VDEC_SOC (VDE) |
| VDEC_LAT_VLD | 0x16011000 | 0x400 | VDEC_SOC |
| VDEC_RACING | 0x16004000 | 0x1000 | VDEC_CORE `[I]` (not stated in K; it is restored by the core clock-on path, `mtk_vcodec_dec_pm.c`:204-213) |
| VDEC_SOC_GCON | 0x1600f000 | 0x1000 | VDEC_SOC |
| VDEC_ALL (ABI rev 3, DEC07) | 0x16000000 | 0x30000 | per 4 KiB page, see below |

`RWVC_WIN_VDEC_ALL` is the vendor decoder's whole 0x16000000 mapping as one window (48 pages). The kernel picks the
block per page: 0x00-0x0c `VDEC_CORE` (base, racing, the vendor's low pages), 0x0d `VDEC_SOC` (larb5), 0x0e
`VDEC_CORE` (larb6), 0x0f-0x1f `VDEC_SOC` (`VDEC_SOC_GCON`, LAT and the vendor's LAT sub-blocks), 0x20-0x2f
`VDEC_CORE` (`VDEC_VLD`…`VDEC_SYS`, larb4). `VDEC_CORE` takes a hidden reference on `VDEC_SOC`, so a core page implies
both domains; a SOC page works with only VDE on. Because the vendor's own decoder page list is only partly named by
open sources, every unnamed low page is mapped to the core (the narrower allowance) `[I]`; DEC02's measured page list
can refine single pages in `n2vc_vdec_page_blk()`.

The two larb pages are kernel-owned (`n2vc_larb_on()` programs them) and are **read-only except one bit each**:
`SMI_LARB_SLP_CON` is 0xc (`K/drivers/misc/mediatek/smi/smi_reg.h`:70); larb5 0x1600d00c and larb4 0x1602e00c accept
a write whose changed bits are all inside bit 0 (`wmask` 1) and refuse anything else with `EACCES`. That is the
sleep-protect handshake `vdec_hal_wait_larb_sleep_protect`/`release_larb_sleep_protect` performs (write bit 0, poll
bit 16); refusing it would hang the vendor's first frame (DEC04/opus-dec1/opus-dec2's traces). Unknown offsets are
refused, never silently allowed. The tree's `include/redwood_vcodec.h` is still rev 2, so `nord2_vcodec.inc` carries a
six-line scaffold (`RWVC_WIN_VDEC_ALL 12`, `N2VC_WIN_N 13`) that disappears when hw-lead lands rev 3.

## 1. The SPM power domain sequence (all four blocks)

Common numbers (`K/drivers/clk/mediatek/clk-mt6885-pg.c`, pg.c from here on):

| Item | Value | pg.c line |
|---|---|---|
| SPM base | 0x10006000 | dts:957 |
| `POWERON_CONFIG_EN` | SPM + 0x000 | :166 |
| `PWR_STATUS` / `PWR_STATUS_2ND` | SPM + 0x16c / 0x170 | :167-168 |
| `VDE_PWR_CON` / `VDE2_PWR_CON` / `VEN_PWR_CON` / `VEN_CORE1_PWR_CON` | SPM + 0x33c / 0x340 / 0x344 / 0x348 | :186-189 |
| `DIS_PWR_CON` (the parent display domain) | SPM + 0x350 | :191 |
| `PWR_RST_B`, `PWR_ISO`, `PWR_ON`, `PWR_ON_2ND`, `PWR_CLK_DIS` | bits 0, 1, 2, 3, 4 | :265-270 |
| `SRAM_PDN` / its ack bit | bit 8 / bit 12 | :620-631 |
| `VEN/VDE/VDE2/VEN_CORE1_PWR_STA_MASK` | bits 17, 15, 16, 18 of 0x16c and 0x170 | :557-560 |
| project code in `POWERON_CONFIG_EN` | `(0xB16 << 16) \| 1` | :262, written at :2427/:2539/:2655/:2801 |
| infracfg base | 0x10001000 | dts:958 |

Bus-protection registers (infracfg, pg.c:225-241): `MM` 0x2d0 (SET 0x2d4, CLR 0x2d8, ack/STA1 0x2ec) and `MM_2` 0xdc8
(SET 0xdcc, CLR 0xdd0, ack/STA1 0xdd8).

| Domain | CON | step-1 bits | step-2 bits | register + ack |
|---|---|---|---|---|
| VEN | 0x344 | 0x2d8 bit 26 **and** 0xdd0 bit 0 | 0x2d8 bit 27 **and** 0xdd0 bit 1 | MM and MM_2, ack 0x2ec/0xdd8 |
| VEN_CORE1 | 0x348 | 0x2d4 bits 28\|30 | 0x2d4 bits 29\|31 | MM only |
| VDE | 0x33c | 0x2d8 bit 24 | 0x2d8 bit 25 | MM only |
| VDE2 | 0x340 | 0xdd0 bit 6 | 0xdd0 bit 7 | MM_2 only |

Bit definitions: pg.c:387-402 (`VDE_PROT_STEP1_0_MASK` :387, `VDE2_PROT_STEP...` :391-394, `VEN_PROT_STEP...` :395-402,
`VEN_CORE1_PROT_STEP...` :403-410). Ack masks are the same bits. Note that VEN's two halves live in *different* registers
(MM bit 26/27, MM_2 bit 0/1): `spm_mtcmos_ctrl_ven` :2646-2790 writes `INFRA_TOPAXI_PROTECTEN_MM_SET/CLR` for 26/27 and
`INFRA_TOPAXI_PROTECTEN_MM_2_SET/CLR` for 0/1.

### 1.1 Power on (per domain; `spm_mtcmos_ctrl_ven` :2646-2790, `_ven_core1` :2792-2919, `_vde` :2418-2528, `_vde2` :2530-2644)

Write order; every wait has a timeout in this driver (2000 × 10 µs = 20 ms; M1 uses the same):

1. `SPM+0x000 = 0xB160001` (enable SPM register control).
2. `CON |= PWR_ON` (bit 2), then `CON |= PWR_ON_2ND` (bit 3).
3. Wait `0x16c & sta_bit` and `0x170 & sta_bit` both set. On timeout: clear bits 2 and 3 again (withdraw the request) and
   fail. (The Linux code loops forever; ours does not.)
4. `CON &= ~PWR_CLK_DIS` (bit 4), `CON &= ~PWR_ISO` (bit 1), `CON |= PWR_RST_B` (bit 0), `CON &= ~SRAM_PDN` (bit 8).
5. Wait ack bit 12 == 0 (SRAM out of power-down). On timeout: leave the domain powered, protection still set, and fail.
6. Release bus protection, **step 2 first, then step 1**, and do not wait for acks (Linux ignores them here):
   - VEN: `MM_CLR(0x2d8) = bit 27`, `MM_2_CLR(0xdd0) = bit 1`, `MM_CLR = bit 26`, `MM_2_CLR = bit 0`.
   - VEN_CORE1: `MM_CLR = bits 29|31`, then `MM_CLR = bits 28|30`.
   - VDE: `MM_CLR = bit 25`, then `MM_CLR = bit 24`.
   - VDE2: `MM_2_CLR = bit 7`, then `MM_2_CLR = bit 6`.
   (pg.c :2766-2787 for VEN, :2905-2916 for VEN_CORE1, :2516-2526 for VDE, :2630-2640 for VDE2.)
7. Verify that all protection status bits of this domain read 0 and that the status bits, `CON & 0x1f == 0x0d`, no SRAM_PDN
   and no ack remain. M1's `n2j_domain_on()` is the model (`kernel/b/bsd/dev/nord2_hwcodec.inc`).
8. Only now touch the block's gate registers, LARB ports and MMIO windows.

Parent: the display domain DIS (SPM+0x350, status bit 20) must be on. Reason: the video LARBs hang off the display's SMI
common 0x1411f000 (dts:5891, its `clocks` include `SCP_SYS_DIS` :5896-5902), and the camera bring-up lost the system
27-44 s after releasing a domain's protection while its SMI parent was off (`bsd/dev/nord2_seninf.inc`:152). The SPM
parent of VDE/VEN itself is not stated in K `[I]`; this check is the conservative one M1 already does. M1 found DIS on.

Additional parent checks (opus-kern audit) before any write:

- **VENC_C1 and VDEC_CORE** also require **SPM status bit 19 (MDP, `MDP_PWR_CON` 0x34c, pg.c:190/:561) set in both 0x16c
  and 0x170**, else `EACCES`. `smi_drv.c`:198-232 puts larb7 and larb5 on the display's SMI common (0x1411f000, larb ids
  21/24/25) but **larb8 (VENC core 1) and larb4 (VDEC core) on the MDP SMI common** 0x1f002000 (larb ids 22/26/27, clocks
  `SCP_SYS_MDP` + `CLK_MDP_SMI1/2`; dts `mdp_smi_common@1f002000`); iommu1 0x1f027000 is the MDP IOMMU. Releasing their bus
  protection with MDP off is the camera's hang pattern.
- **All four blocks** require the SMI common's clock root to run: `CLK_CFG_1` (topckgen 0x020) `disp_sel` gate bit 7 must
  be 0, and for the MDP blocks `mdp_sel` gate bit 15 must be 0 (`clk-mt6885.c`:1693-1702, `MUX_GATE_CLR_SET_UPD`; the pdn
  bit is set = gated). The phone's boot log (`runs/q10hw1/00-step0.log`) has MDP and DIS on (`PWR_STATUS 0x10580e03`).

### 1.2 Power off (reverse; `spm_mtcmos_ctrl_ven` :2660-2730, `_ven_core1` :2806-2860, `_vde` :2432-2480, `_vde2` :2544-2592)

1. Gate the block's clocks first — while the domain is still on and unprotected (§3).
2. `SPM+0x000 = 0xB160001`.
3. Set bus protection, **step 1 first, then step 2**, waiting for each ack in `0x2ec`/`0xdd8` (20 ms bound):
   - VEN: `MM_SET(0x2d4) = bit 26` ack, `MM_2_SET(0xdcc) = bit 0` ack, `MM_SET = bit 27` ack, `MM_2_SET = bit 1` ack.
   - VEN_CORE1: `MM_SET = bits 28|30` ack, `MM_SET = bits 29|31` ack.
   - VDE: `MM_SET = bit 24` ack, `MM_SET = bit 25` ack.
   - VDE2: `MM_2_SET = bit 6` ack, `MM_2_SET = bit 7` ack.
4. `CON |= SRAM_PDN` (bit 8), wait ack bit 12 == 1.
5. `CON |= PWR_ISO` (1), `CON |= PWR_CLK_DIS` (4), `CON &= ~PWR_RST_B` (0), `CON &= ~PWR_ON` (2), `CON &= ~PWR_ON_2ND` (3).
6. Wait both status registers show the bit clear. On a protection ack timeout: stop before step 4, leave the domain
   powered, and report the error — never continue into SRAM power-down with protection not acknowledged.

## 2. Top clock muxes

Topckgen base 0x10000000 (dts:959), `CLK_CFG_UPDATE1` = 0x008 (`K/drivers/clk/mediatek/clk-mt6885.c`:34). Each mux has a
SET and a CLR register and a gate bit inside the mux word (`MUX_GATE_CLR_SET_UPD`, clk-mt6885.c:1958-1974); a parent is
selected by writing the parent number to the field in the CLR register (clears the other parents) and then pulsing the
update bit.

| Mux | Config word | Parent field | Gate bit | Update bit | Parent list |
|---|---|---|---|---|---|
| `venc_sel` | 0x0e0 (SET 0x0e4, CLR 0x0e8) | bits 27:24 (4 bits) | 31 | 0x008 bit 24 | clk-mt6885.c:1221-1238 |
| `vdec_sel` | 0x0f0 (SET 0x0f4, CLR 0x0f8) | bits 3:0 | 7 | 0x008 bit 25 | clk-mt6885.c:1240-1257 |
| `vdec_lat_sel` | 0x0f0 | bits 11:8 | 15 | 0x008 bit 26 | clk-mt6885.c:1259-1276 |

Line evidence: register addresses clk-mt6885.c:75-80; shifts :147-149; the mux descriptors :1958-1974 (lsb/width/pdn/update
arguments). `venc_ck = venc_sel / 1`, `vdec_ck = vdec_sel / 1`, `vdec_lat_ck = vdec_lat_sel / 1` (:596-601).
Write semantics (`K/drivers/clk/mediatek/clk-mux.c`): a parent change is `write(clr_ofs, 0xf << shift)` then
`write(set_ofs, parent << shift)` then `write(upd_ofs, 1 << upd)` (:178-216); the **gate bit lives in the CLR/SET
registers too**, `write(clr_ofs, 1 << gate)` enables and `write(set_ofs, 1 << gate)` disables (`:78-118`, `:248-251`).
So the driver's `CLR = 0x8f << 24` clears the four parent bits *and* enables `venc_sel` in one write, which is what a
power-on needs; the read-back below requires gate bit 31 clear and the wanted parent.

Parents used here:
- `venc_parents[4] = mainpll_d4_d2 = 273 MHz` (`clk-mt6885.c`:1226) and `venc_parents[12] = univpll_d4 = 624 MHz`
  (`clk-mt6885.c`:1234). `mt6893.dts`:5037-5040 lists the mmdvfs VENC steps 624/416/312/273 MHz; the driver's choice is
  the lowest step (`venc_step3`), the only one paired with the lowest vcore OPP. The phone's bootloader leaves parent 12
  in `CLK_CFG_13` (`0x0c030601`: gate bit 31 clear, parent 12) and M1 ran the JPEG encoder with it (`runs/q10hw1/*.log`).
- Two owners, two parents, decided by the lead on 11 November for the first phone encode: **vcodecd's `RWVC_POWER`
  selects parent 4 (273 MHz)** (`N2VC_VENC_SLOW`, a compile-time knob in `nord2_vcodec.inc`; setting it to 0 restores
  624 MHz), **the JPEG driver's shared `n2vc_ven_get()` selects parent 12 (624 MHz)**, the value its M1 test ran with.
  Both writes happen only while VEN and VENC_C1 are off; a domain that is already on is never re-muxed. Sequence
  (`n2vc_venc_mux`): `CLR = 0x8f << 24`, `SET = parent << 24`, `UPDATE1 = 1 << 24`, 100 us, then read back and require
  gate clear + the wanted parent. `CLK_CFG_13` bits 0-23 belong to the `aud_2_sel`, `adsp_sel` and `dpmaif_main_sel`
  muxes and are never written (the CLR/SET masks only touch 27:24 and 31).
- Why 273 MHz for the encoder: the alternative is 624 MHz (`venc_step0`) while the vcore OPP is whatever the boot left;
  a long encode at the top step on a low vcore OPP could fail (`[I]`, `kern/p031x/STATUS.md`:101-105). Why 624 MHz for
  JPEG: that is what M1 measured (1080p in 4.6 ms) and what the phone boots with, so the JPEG path is unchanged.
- Interaction: if the JPEG driver holds the shared VEN reference while a vcodec job runs, the vcodec job runs at the
  parent the JPEG path selected (a running domain is not re-muxed); the next power-on by either owner re-selects its own
  parent. The host test covers both directions.
- `vdec_sel`: parent 14 = `univpll_d4` (clk-mt6885.c:1255), same clock as `venc_sel` on the phone. `vdec_lat_sel`: parent
  14 = `univpll_d4` (clk-mt6885.c:1274). Both are left alone if already ungated with a non-zero parent.
- Why `univpll_d4` for VDEC: univpll is the PLL the running display uses; the phone booted with `venc_sel` on
  `univpll_d4`, so it is on and its rate is known to work. `[I]` for VDEC.

`vdec_lat_sel` uncertainty `[I]`: on MT6893 the VDE1 (SOC/LAT) gate provider declares **`vdec_ck`** as the parent of all
its gates, including `CLK_VDE1_LAT_CKEN` (`K/drivers/clk/mediatek/clk-mt6885-vde.c`:214-232), while `vdec_lat_sel` exists
and feeds `vdec_lat_ck` (clk-mt6885.c:600-601). This driver therefore programs both `vdec_sel` and `vdec_lat_sel` to the
same safe parent when they are gated; if the LAT block turns out to be fed only by `vdec_sel`, writing the other mux is
harmless (no other block uses either).

## 3. Clock gates

MTK gates are inverted (`mtk_clk_gate_ops_setclr_inv`, `K/drivers/clk/mediatek/clk-gate.c`:238-248, :299-308: *enable* =
write 1 to the SET register, *disable* = write 1 to the CLR register; the status register bit is 1 when the clock runs).
The power-on hook after every VEN/VDE power-up also writes one fixed value (`clk-mt6885-pg.c`:1163-1182).

| Domain | Gate block | Status | Set (on) | Clear (off) | Bits used here |
|---|---|---|---|---|---|
| VEN | 0x17000000 | 0x0 | 0x4 | 0x8 | 0 larb, 4 venc, 28 GALS; bit 8 (jpgenc) belongs to K01 and is never written here |
| VEN_CORE1 | 0x17800000 | 0x0 | 0x4 | 0x8 | 0 larb, 4 venc, 28 GALS |
| VDE | 0x1600f000 | 0x0, 0x200, 0x8 | 0x0, 0x200, 0x8 | 0x4, 0x204, 0xc | bit 0 in each: VDEC_CKEN, LAT_CKEN, LARB1_CKEN |
| VDE2 | 0x1602f000 | 0x0, 0x200, 0x8 | 0x0, 0x200, 0x8 | 0x4, 0x204, 0xc | bit 0 in each: VDEC_CKEN, LAT_CKEN, LARB1_CKEN |

Evidence: gate register layout `K/drivers/clk/mediatek/clk-mt6885-ven.c`:27-31 and :86-91 (set 0x4/clr 0x8/sta 0x0), gate
list :103-116 (bits 0,4,8,12,16,28); `clk-mt6885-vde.c`:28-39 (set 0x0/clr 0x4/sta 0x0), :142-151 (0x200/0x204/0x200 and
0x8/0xc/0x8), gate lists :214-232 (VDE1) and :105-128 (VDE2); the post-power-on writes are pg.c:1163-1182
(`VDEC_SOC_CKEN_SET(0x0)=1`, `VDEC_SOC_LAT_CKEN_SET(0x200)=1`, `VDEC_SOC_LARB1_CKEN_SET(0x8)=1`, same for VDE2;
`VENC_CG_SET(0x4)=0x4`, `VENC_C1_CG_SET(0x4)=0x4`). The value `0x4` written by the VEN hook is bit 2 of the SET register,
which is not in the gate provider's list; M1 kept it and it is kept here for fidelity (it is not polled as a status bit).

After enabling, the driver reads the status register and requires the bits to be set (10 ms bound), exactly as M1 does for
JPEG (`n2j_power()` in the model, `N2J_E_GATES` on mismatch).

## 4. LARB configuration

Each video LARB is programmed with the values of `K/drivers/misc/mediatek/smi/mt6885/smi_conf.h` (smi_conf.h from here
on). Register offsets are from `K/drivers/misc/mediatek/smi/smi_reg.h`: `CMD_THRT_CON` 0x24 (:75), `SW_FLAG` 0x40 (:76),
`OSTDL_PORT(p)` = 0x200 + 4p (:100), `INT_SMI_LARB_CMD_THRT_CON` = 0x500 + 0x24 = 0x524 (:112). The port's
`NON_SEC_CON(p)` = 0x380 + 4p (`K/drivers/misc/mediatek/m4u/mt6765/m4u_reg.h`:357, same layout used by
`pseudo_m4u_v2.c`:563-575).

| LARB | Base | Ports | Conf pairs (offset = value) | OSTDL per port (0..n-1) |
|---|---|---|---|---|
| larb7 (VEN) | 0x17010000 | 27 | 0x24 = 0x300256, 0x40 = 1, 0x524 = 0x300256 (smi_conf.h:95-99) | smi_conf.h:247-276: 1,4,1,1,1,1,1,1,1,4,4,1,4,1,0xa,6,1,0xa,6,1,1,1,1,5,3,3,4 |
| larb8 (VEN_C1) | 0x17810000 | 27 | same as larb7 (smi_conf.h:130: table index 8 → larb7 pair) | smi_conf.h:277-305: 1,4,1,1,1,1,1,1,1,4,4,1,4,1,0xa,6,1,0xa,6,1,1,1,1,5,3,3,4 |
| larb4 (VDE2) | 0x1602e000 | 11 | 0x24 = 0x300256, 0x40 = 1 (smi_conf.h:86-90) | smi_conf.h:220-232: 0x28,0x19,0xb,1,1,1,1,1,1,4,1 |
| larb5 (VDE) | 0x1600d000 | 8 | same as larb4 (smi_conf.h:129: table index 5 → larb4 pair) | smi_conf.h:234-243: 1,1,4,1,1,1,1,0x16 |

Port numbers: `K/drivers/misc/mediatek/smi/mt6885/smi_port.h`:25-29 (larb4 11, larb5 8, larb7 27, larb8 27).

Which ports belong to whose block (`K/include/dt-bindings/memory/mt6885-larb-port.h`):

| LARB | VENC/VENC_C1 ports (this driver) | Other users (not written by this driver) |
|---|---|---|
| larb7 | 0-6 (`VENC_RCPU/REC/BSDMA/SV_COMV/RD_COMV/NBM_RDMA/NBM_RDMA_LITE` :91-97), 13-22 (`NBM_WDMA`, `CUR_LUMA`, `CUR_CHROMA`, `REF_LUMA`, `REF_CHROMA`, `SUB_R_LUMA`, `SUB_W_LUMA`, `FCS_NBM_RDMA`, `FCS_NBM_WDMA` :104-113) | 7-12 and 23-26: JPEG encoder 0 (K01) |
| larb8 | 0-6, 13-22 (:119-141) | 7-12, 23-26: JPEG encoder 1 |
| larb4 | 0-10, all (`VDEC_MC/UFO/PP/PRED_RD/PRED_WR/PPWRAP/TILE/VLD/VLD2/AVC_MV/RG_CTRL_DMA` :69-79) | — |
| larb5 | 0-7, all (`VDEC_LAT0_*`, `UFO_ENC` :81-88) | — |

Each of the driver's ports is switched to **physical addressing** (IOMMU bypassed): clear bit 0 and bits 8-15 of
`NON_SEC_CON(p)` (`pseudo_m4u_v2.c`:571-573 writes `old & ~F_SMI_ADDR_BIT32 & ~F_SMI_MMU_EN` for Virtuality 0), then read
it back and require the result. For **larb7 and larb8 only**, bits 16-19 are cleared as well (mask 0xfff01): Linux routes
VENC ports 5,6,13,14,21,22 to MMSRAM with `smi_sysram_enable` (`smi_drv.c`:498-524, called from `mtk_vcodec_enc_pm.c`:299-326,
`NON_SEC_CON` bits 16-19 = 0xf), and this kernel does not power MMSRAM. larb4/larb5 keep bits 16-19 (mask 0xff01). This
is the method the camera driver already uses on larbs 13/14 (`bsd/dev/nord2_seninf.inc`:98-103, `nord2_camsv.inc`:18-33),
so the address registers may only hold < 4 GB physical addresses — which the pool does (`RW_CLAIM_VCODEC_PA`). The Android
encoder rides `Virtuality = 1` (IOMMU) on these ports; we bypass the IOMMU for all of them because the M4U is not
programmed for the video larbs in this kernel.

OSTDL is written only for the driver's own ports, then `NON_SEC_CON`; the LARB conf words (0x24/0x40/0x524) are shared with
the JPEG driver and written with identical values from smi_conf.h, so the two writes are idempotent. The display's SMI
common (0x1411f000) is **not written** (M1 did not either).

## 5. Interrupt registers and the polled emulation

Linux's ISRs:

| Line | Status register | Done bit | Clear sequence | Extra check |
|---|---|---|---|---|
| VENC (SPI 308) | VENC base + 0x05c | any of 0x1 SPS, 0x2 PPS, 0x4 FRM, 0x8 DRAM, 0x10 PAUSE, 0x20 SWITCH, 0x80 VPS | write the set bits to base + 0x060 (write-1-to-clear), one bit at a time in Linux | none |
| VENC core 1 (SPI 313) | 0x17820000 + 0x05c `[I]` (same IP, separate `venc@17820000` node; Linux's handler only ever uses `enc_reg_base[VENC_SYS]`, enc_reg_base[0] = 0x17020000) | same bits | 0x17820000 + 0x060 | — |
| VDEC core (SPI 424) | `VDEC_MISC` 0x16025000 + 0xa4 | bit 16 (0x10000) DEC_SUCCESS | `write(read \| 0x11)`, then `write(read & ~0x10)` | `VDEC_SYS` 0x1602f000 bit 4 (0x10) must be 0 ("hw active") or the ISR returns |
| VDEC LAT (SPI 425) | `VDEC_LAT_MISC` 0x16010000 + 0xa4 | bit 16 | same | Linux checks `VDEC_BASE` 0x16000000 bit 4 `[I]` (its handler reads `dec_reg_base[VDEC_BASE]`) |

Evidence: `K/drivers/media/platform/mtk-vcodec/mt6885/mtk_vcodec_intr.h`:16-31 (`MTK_VDEC_IRQ_CFG` 0x11, `IRQ_CLR` 0x10,
`CFG_REG` 0xa4, `DEC_SUCCESS` 0x10000, the VENC bit list, status offset 0x5c, ack offset 0x60);
`mtk_vcodec_intr.c`:61-109 (VDEC handler), :111-153 (LAT), :156-187 (`clean_irq_status`), :189-215 (VENC handler, status at
`enc_reg_base[VENC_SYS]+0x5c`, ack at +0x60); `mt6885/mtk_vcodec_pm.h`:73-77 (`VENC_SYS=0`, `VENC_C1_SYS=1`); the two reg
bases come from the DTS reg list of `venc@17000000` (dts:5755-5756).

Polled mode (`RWVC_WAIT_IRQ` while `irq_mode == 0`): read the status register, then sleep 500 µs with
`msleep(&n2vc, &n2vc_mtx, PZERO, "n2vc-wait", &ts)` — which **drops `n2vc_mtx`** — and repeat up to `timeout_ms` (≤ 5000),
measured with `mach_absolute_time()`/`absolutetime_to_nanoseconds()`, not by counting sleeps. After every wake the file
(`f->used` and its open id, because a destroyed slot can be reused by a new open) and the line's block (`n2vc.on[blk]`) are
re-validated before the next register read: `ENXIO` if the file was
closed, `EACCES` if the block was powered off. Match VENC on `status & 0xbf` / VDEC on `status & 0x10000`, record the raw
status in `out_status`, then clear exactly as the ISR above does. A timeout returns `ETIMEDOUT`, `out_count = 0` and the last
raw status. `RWVC_IRQ_CLEAR` clears the same bits unconditionally if the block is powered and resets the per-file count.
`irq_mode 1` is **not implemented**; the proposal to hw-ik is in §8.

## 6. DMA pool

`RW_CLAIM_VCODEC_PA = 0xda800000`, `RW_CLAIM_VCODEC_BYTES = 0x05600000` (86 MiB, `include/redwood_vcodec.h`), above the
JPEG claim 0xd8000000..0xda800000 and below 0xdfe00000; nothing is carved out in 0xc0000000..0xdfe70000 (`[V]` from the
live device tree, the same fact M1 used for 0xd8000000). 22016 pages of 4 KiB, first-fit allocation, one bitmap, roving
hint, 64-byte-aligned cache maintenance (`dc civac` per line + `dsb sy`) after a user→buffer write and before a
buffer→user read. Handles are `(generation << 12) | (slot + 1)`, generation bumped on free, 1024 slots, at most 128 live
handles per open file. Access is "allocated by this file, or `RWVC_DMA_SHARED` and the caller is on the other minor"; only
the owner may free.

`RWVC_DMA_INFO` (header rev 2) returns `bytes` and `out_pa` for a handle under exactly that rule: `EINVAL` handle 0,
`ENOENT` unknown or stale generation, `EPERM` not usable by the caller (not shared, or a different client on the same
minor), `ETIMEDOUT`/`EFAULT` as for any ioctl. The server uses it to learn a client buffer's physical address. The ioctl
is allowed on both minors (lead's decision); the access rule is what restricts it.

`RWVC_DMA_RW` copies in chunks of at most **1 MiB** (`copyin`/`copyout` per chunk, `dc civac`+`dsb sy` after each write
chunk and before each read chunk). The mutex is held across the whole call, so the handle cannot go stale mid-copy; the
chunking only bounds how long one copy keeps the relay/JPEG callers out.

## 7. `RWVC_REGS` semantics decided here (not in the frozen header)

- A batch is bounded to **2 s of wall time in total** (DELAY_US plus POLL time); when the budget is exhausted the batch
  stops with `ETIMEDOUT` and `out_done` = number of ops completed. Without this a client could hold the device for
  2048 × 1 s.
- `POLL` reads once immediately, then sleeps 500 µs (`msleep`, mutex dropped) between reads, up to
  `min(poll_timeout_us, 1 s, remaining budget)` of measured time; `poll_timeout_us` 0 means one read. After each wake
  `f->used`/its open id and the window's block are re-checked: `ENXIO`/`EACCES` instead of touching hardware that another thread took
  away. For `RWVC_WIN_VDEC_ALL` the re-check is per page (`n2vc_op_block()`), not per window: a poll on a core page
  fails `EACCES` if the core went off, while a poll on a SOC page only needs VDE. `DELAY_US` value ≤ 1000 else `EINVAL`; it is executed as chunks of ≤ 100 µs of `delay()`.
- On the first failing op, ops before it have executed and their `READ` results are copied out; `out_done` counts only
  fully executed ops.
- Errors: `EINVAL` bad field/alignment/range, `EACCES` the window's block is not powered (also: powered off while
  sleeping), `EFAULT` bad user pointer, `ETIMEDOUT` poll/budget, `ENXIO` the window could not be mapped or the file was
  closed while sleeping.
- `RWVC_POWER`: per open file and per block; `off` without a matching `on` is `EINVAL`; a block that implies another one
  (VENC_C1 → VEN, VDEC_CORE → VDEC_SOC) takes a hidden reference on it for the lifetime of the child; all references of an
  open file are dropped on close. Errors: `EACCES` if the display (parent) domain is off, `ETIMEDOUT` for a power/SRAM/protection
  ack, `EIO` for a mux or read-back mismatch.
- Minor policy: minor 0 gets everything; minor 1 gets `RWVC_INFO`, `RWVC_DMA_ALLOC/FREE/RW/INFO` only (`EACCES` otherwise).
  `RWVC_DMA_FILL` is minor-0 only. `RWVC_MSG_*` and the minor-1 node belong to hw-ik's relay (§9). A handle allocated with
  flags bit 0 is usable from **the other minor** only (the relay's `n2vr_dma_allowed()` is applied verbatim); two clients
  on minor 1 never share one, and only the allocator frees it.

## 8. Real interrupt proposal for hw-ik (not implemented, not compiled)

`gpu/build/autotest/q10at3/kernel/stage/bsd/dev/nord2_gpu_irq.h` (MIT) already contains the reviewed GICv3 SPI plumbing the
GPU uses: `nord2_gic_intid_for_spi()` (:63), `nord2_gic_irouter_for_mpidr()` (:68), `nord2_gic_spi_valid()` (:73),
`nord2_gic_spi_configure()` (:106-127, level, Group 1, routed to one PE, still disabled, verifies priority/route/level) and
`nord2_irq_guard_note()` (:131-142). `iokit/Kernel/arm/Nord2GPU.cpp`:586-640 shows the whole pattern: map GICD at
`/arm-io/gic` + 0x01000000, `nord2_gic_spi_configure(&g, intid, irouter, 0xa0)`,
`ml_install_interrupt_handler(this, 0, this, dispatch, NULL)` with preemption disabled, then arm the device line and
confirm the SPI is pending before enabling it, and disable it again when the spurious guard trips.

Proposed for `/dev/redwood-vcodec` (vcodecd would set `irq_mode = 1` in `RWVC_INFO` once this exists):

```c
/* in nord2_vcodec.inc, #ifndef N2VC_HOST; SPI = RWVC_IRQ_VENC 308, VENC_C1 313, VDEC 424, VDEC_LAT 425 */
static const unsigned n2vc_spi[RWVC_IRQ_COUNT] = { 308, 313, 424, 425 };
static uint64_t n2vc_irq_mpidr(void) { return __builtin_arm_rsr64("MPIDR_EL1"); }
/* map GICD exactly as Nord2GPU::irqSetup() does; keep struct nord2_gic_ops in a static */
static void n2vc_irq_top(void *pc /* IOKit handler context */, void * /*refCon*/, void * /*src*/, int source)
{
    unsigned line = (unsigned)(uintptr_t)pc;                  /* per-line cookie, one handler per SPI */
    uint32_t st = n2vc_irq_read_status(line);                 /* §5 table */
    if (!nord2_irq_guard_note(&n2vc_guard[line], n2vc_irq_match(line, st))) { n2vc_spi_enable(line, false); }
    n2vc_irq_count[line]++;
    n2vc_irq_last[line] = st;
    n2vc_irq_clear_raw(line, st);                             /* the same writes as §5 */
    wakeup(&n2vc_irq_wait[line]);
}
/* RWVC_WAIT_IRQ: if irq_mode == 1 and the line is active:
 *   for (;;) { if (n2vc_irq_count[line] != seen) { ... } msleep(&n2vc_irq_wait[line], &lock, PZERO|PCATCH, "n2vc-irq", &ts); }
 * with the same 500 µs sleep poll fallback if the line is not active (lost interrupt costs latency, never correctness). */
```

Open questions for hw-ik before this is written (do not guess): which PE runs the daemon and whether
`ml_install_interrupt_handler` may be called after boot from a normal thread (the GPU calls it from its start path); whether
two handlers (one per SPI) can be installed; and whether the four SPI numbers can be confirmed from the GICD pending state
the way the GPU confirms SPI 364. Until then `irq_mode` stays 0 and every wait is polled, which is correct but slower.

**UPDATE (opus-kern audit, 11 Nov):** `ml_install_interrupt_handler` stores **one handler per CPU**
(`osfmk/arm64/machine_routines.c`:801-821), so a second install on the GPU's PE would replace `Nord2GPU::irqDispatch`, not
coexist with it. A real VENC/VDEC IRQ therefore needs a chained dispatcher inside the GPU driver (or a different PE), not
another `ml_install`; v1 stays polled with the 500 us `msleep`. SPI 308 -> INTID 340 is confirmed from the dts.

Read-only evidence for the phone bring-up: on the first completion of VENC / VENC_C1 per boot the driver logs the GICD
`ISPENDR` bit of INTID 340 / 345 (GICD 0x0c000000 + 0x200, read before the ack; `-1` if GICD could not be mapped). That
shows whether the dts SPI number is the line that actually asserts, without installing a handler.


## 9. Integration seam (what the other kernel packages call)

The hardware file is `kern/bsd/dev/nord2_vcodec.inc` (K02); it is MIT and is included once by `sweet_touch.c` **after**
hw-ik's `nord2_vcodec_relay.inc` and **before** hw-k01's `nord2_jpegdev.inc` (the include order in
`sweet_touch.c.patch`). What it exports:

| Symbol | Used by | Meaning |
|---|---|---|
| `void nord2_vcodec_start(void)` | `nord2_input_init` (IK's patch) | `cdevsw_add` + `/dev/redwood-vcodec` (minor 0, 0600), then `n2vr_start(&hw)` for the client node, then `n2jd_share_ven(n2vc_ven_get, n2vc_ven_put)` |
| `struct n2vc_open *n2vc_open_create(unsigned minor)` / `void n2vc_open_destroy(struct n2vc_open *)` | optional, for another cdevsw | per-open state; destroy frees its DMA handles and drops its power references |
| `int n2vc_ioctl(struct n2vc_open *, u_long, caddr_t, int, struct proc *)` | hw-ik's relay (`hw.ioctl`) | the hardware ioctls; enforces the minor policy above |
| `void n2vc_info_fill(struct rwvc_info *)` | hw-ik's relay (`hw.info`) | the hardware fields of `RWVC_INFO` (`server_present` from `n2vr_server_present()`) |
| `void n2vc_relay_closed(struct proc *)` | hw-ik's relay, at a client's last close | frees that client's DMA handles and references; **IK must add one call** (posted on the board) |
| `int n2vc_ven_get(void)` / `void n2vc_ven_put(void)` | hw-k01's JPEG driver | the shared VEN reference; `get` performs the full §1.1/§3 sequence on the first reference (including `venc_sel` = parent 12, 624 MHz, when VEN is off), `put` powers down on the last |
| `unsigned n2vc_minor_opens(unsigned)` / `uint32_t n2vc_powered_mask(void)` | diagnostics | open counts per minor and the real powered mask |

`n2vr_server_open()`/`n2vr_server_close()` are called by the minor-0 `d_open`/`d_close` of this file, and minor-0
`RWVC_MSG_*` is handed to `n2vr_ioctl()` before the hardware switch (the relay's server side). The host test
(`kern/test_k02`) builds the hardware half against the register model; the kernel glue above is compiled by IK's
`compile_check.sh` (a local staged compile of the full `sweet_touch.c` with the CI flags already succeeds).

Host test: `bash gpu/hwcodec/kern/test_k02/run_tests.sh` → `N2TEST K02 PASS 588871/588871 (violations 0)` under ASan+UBSan
and -O2. It covers all four blocks' power-on/off and fault injection (including the MDP-off refusal for VENC_C1/VDEC_CORE),
LARB port ownership and the 0xfff01 VENC mask, the 0x3000 VENC windows, the 2048-op register batches, polled IRQs,
power-off and close during a sleeping WAIT_IRQ/POLL (the model counts every window read or write after the block went
off), `RWVC_DMA_INFO` for own/shared-other-minor/same-minor/unknown/stale handles, the 1 MiB-chunked `RWVC_DMA_RW`, the
multi-slot `d_close` semantics, the DMA pool's 10000-op property test, close with outstanding state, and the shared-VEN
reference counting.
