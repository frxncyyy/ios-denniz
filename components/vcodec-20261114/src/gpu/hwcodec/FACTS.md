# FACTS.md -- everything the Nord 2 hardware-codec effort knows, one fact per line

Owner: `hw-d01` (the only editor). Format: **statement** -- evidence -- **[V]** verified today from the named
artifact (path/line, symbol, command output) or **[I]** inferred -- **who** reported it (a worker id, or `lead`
for `PLAN.md`/`STATUS.md`). A line without evidence or without a mark is a defect: post it to `hw-d01`.
Sources merged: `PLAN.md` §1-2, `STATUS.md`, `runs/q10hw1/` (the one phone run), and board reports from
hw-k02, hw-v03, hw-v10, hw-v12, hw-v14, hw-v40 (as credited). Nothing here was run on the phone unless a
`runs/` log is named.

## 1. Hardware: the blocks and who programs them

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 1.1 | The SoC is a MediaTek MT6893 (Dimensity 1200): 4x A78 + 4x A55, Mali-G77, VENC/VDEC/JPEG blocks as below. | `K/arch/arm64/boot/dts/mediatek/mt6893.dts`, `cupida_defconfig` | [V] | lead |
| 1.2 | JPEG encoder: `jpgenc@17030000`, 0x10000 bytes, `compatible = "mediatek,jpgenc"`, GIC SPI 309, larb7, clock `venc_gcon_clk CLK_VEN1_CKE2_JPGENC`, M4U ports L7 7/8/9/10. | `K/arch/arm64/boot/dts/mediatek/mt6893.dts`:5771 | [V] | lead |
| 1.3 | The JPEG encoder is programmed entirely by the kernel driver `mtk-jpeg`; the user-space buffer comes back as a complete JPEG file. | `K/drivers/media/platform/mtk-jpeg/mtk_jpeg_core.c`:2231, `mtk_jpeg_enc_hw.c` | [V] | lead |
| 1.4 | JPEG encoder registers: RSTB 0x100, CTRL 0x104, QUALITY 0x108, BLK_NUM 0x10C, BLK_CNT 0x110, INT_STS 0x11C, DST_ADDR0 0x120, DMA_ADDR0 0x124, STALL_ADDR0 0x128, OFFSET_ADDR 0x138, RST_MCU_NUM 0x150, IMG_SIZE 0x154, DEBUG_INFO0/1 0x160/0x164, TOTAL_CYCLE 0x168, BYTE_OFFSET_MASK 0x16C, SRC_LUMA_ADDR 0x170, SRC_CHROMA_ADDR 0x174, STRIDE 0x178, IMG_STRIDE 0x17C, DCM_CTRL 0x300, CODEC_SEL 0x314, ULTRA_THRES 0x318. | `K/drivers/media/platform/mtk-jpeg/mtk_jpeg_enc_reg.h` | [V] | lead |
| 1.5 | JPEG CTRL bits: enable 0, int enable 2, YUV format 3-4 (0 YUYV, 1 YVYU, 2 NV12, 3 NV21), JFIF/EXIF 5, restart 10, RDMA padding 20, right padding 29, padding-with-0 30; INT_STS done bit 1, stall bit 2, vcodec 0x10. | same header | [V] | lead |
| 1.6 | JPEG quality codes (QUALITY low 16 bits): Q30 4, Q38 0, Q44 0xE, Q52 0xA, Q60 1, Q72 0xF, Q78 6, Q85 2, Q90 7, Q95 3, Q97 0x10; quantisation tables are fixed per code. | `K/drivers/media/platform/mtk-jpeg/mtk_jpeg_core.h`:76-92 | [V] | lead |
| 1.7 | JPEG one-encode sequence: RSTB 0 then 1, CODEC_SEL 0; IMG_SIZE = (w<<16) OR h; format in CTRL; BLK_NUM = (pad16(w)/16)*(pad16(h)/16)*6-1 for 4:2:0; IMG_STRIDE = align(even(w),16); STRIDE = bytes/line; luma/chroma addresses; OFFSET_ADDR 0; BYTE_OFFSET_MASK = dst & 15; DST_ADDR0 = dst & ~15; STALL_ADDR0 = (dst+size) & ~15; CTRL gets int-enable, enable and padding set; the file size is DMA_ADDR0-DST_ADDR0. | `K/.../mtk_jpeg_core.c`:1112-1195, :1366-1432 | [V] | lead |
| 1.8 | VEN power domain: SPM 0x10006000, POWERON_CONFIG_EN 0x000 = 0x0B160001, VEN_PWR_CON 0x344, status bits 17 in PWR_STATUS 0x16C / PWR_STATUS_2ND 0x170; on = PWR_ON(2), PWR_ON_2ND(3), clear PWR_CLK_DIS(4), clear PWR_ISO(1), set PWR_RST_B(0), clear SRAM_PDN(8), wait ack(12)==0; then release bus protection (infracfg MM_CLR 0x2D8 bit 27, MM_2_CLR 0xDD0 bit 1) and set `VENC_CG_SET` 0x17000004 = 0x4. | `K/drivers/clk/mediatek/clk-mt6885-pg.c`:2646, :1176 | [V] | lead, hw-k02 |
| 1.9 | SPM register map used: POWERON_CONFIG_EN SPM+0x000 (value (0xB16<<16) OR 1), PWR_STATUS 0x16C / _2ND 0x170, VDE_PWR_CON 0x33C bit15, VDE2 0x340 bit16, VEN 0x344 bit17, VEN_CORE1 0x348 bit18, DIS 0x350 bit20; PWR_ON/ON2ND/CLK_DIS/ISO/RST_B bits 2/3/4/1/0, SRAM_PDN bit8 ack bit12. | `K/drivers/clk/mediatek/clk-mt6885-pg.c`:167-168, :186-191, :262, :265-270, :620-631, :2655 | [V] | hw-k02 |
| 1.10 | Bus protection (infracfg 0x10001000): MM SET 0x2D0 CLR 0x2D4 STA1 0x2EC, MM_2 SET 0xDC8 CLR 0xDD0 STA1 0xDD8; VEN step1 MM bit26 + MM_2 bit0, step2 MM bit27 + MM_2 bit1; VEN_CORE1 step1 bits 28 and 30, step2 bits 29 and 31; VDE 24/25, VDE2 MM_2 6/7; power-on releases step2 then step1, power-off sets step1 then step2. | `K/drivers/clk/mediatek/clk-mt6885-pg.c`:225-241, :387-410, :2432-2526 | [V] | hw-k02 |
| 1.11 | `venc_sel`: topckgen 0x10000000 CLK_CFG_13 0xE0 (SET 0xE4, CLR 0xE8), field bits 27-24, gate bit 31, update CLK_CFG_UPDATE1 0x008 bit 24; parents: 0 tck_26m_mx9_ck, 1 mmpll_d7, 2 mainpll_d6, 3 univpll_d4_d2, 4 mainpll_d4_d2, 5 univpll_d6, 6 mmpll_d6, 7 mainpll_d5_d2, 8 mainpll_d6_d2, 9 mmpll_d9, 10 univpll_d4_d4, 11 mainpll_d4, 12 univpll_d4, 13 univpll_d5. | `K/drivers/clk/mediatek/clk-mt6885.c`:1958, :1221-1235, :147 | [V] | lead |
| 1.12 | vencsys gates 0x17000000 (`clk-mt6885-ven.c`): status 0x0, SET 0x4 = on (inverted), CLR 0x8 = off; bit 0 larb, 4 venc, 8 jpgenc, 12 jpgdec, 16 jpgdec_c1, 28 GALS. | `K/drivers/clk/mediatek/clk-mt6885-ven.c` | [V] | lead |
| 1.13 | larb7 at 0x17010000; `smi_conf.h` values CMD_THRT_CON 0x24 = 0x300256, SW_FLAG 0x40 = 1, 0x524 = 0x300256; OSTDL 0x200+4*port: ports 7,8 = 1, 9,10 = 4. | `K/drivers/misc/mediatek/smi/mt6885/smi_conf.h`:94, :247; `mt6893.dts`:6060 | [V] | lead |
| 1.14 | IOMMU: `SMI_LARB_NON_SEC_CON(port)` = larb + 0x380 + 4*port, bit 0 = translate; with bit 0 clear the port uses physical addresses (the camera driver's proven method on larbs 13/14). | `K/drivers/misc/mediatek/smi/smi_reg.h`:102; `ST/bsd/dev/nord2_seninf.inc`:98-103 | [V] | lead |
| 1.15 | The JPEG DMA address registers are 32 bits wide, so every buffer must live below 4 GB. | `mtk_jpeg_enc_reg.h` (DST_ADDR0/DMA_ADDR0/STALL_ADDR0 u32) | [V] | lead |
| 1.16 | JPEG decoder: `jpgdec@17040000`, three cores, GIC SPI 310/311/315; the kernel writes 21 words supplied by user space (`struct JPEG_DEC_DRV_HYBRID_TASK`) to fixed offsets and triggers with 0x240. | `mt6893.dts`:5788; `K/drivers/misc/mediatek/jpeg/v2/jpeg_drv_dec.c`:30, `jpeg_drv.h`:438 | [V] | lead |
| 1.17 | The 21 hybrid-decoder words are computed by the closed `VFS/lib64/libjpeg-alpha_vendor.so`; the offsets up to 0x274 match the open 2015 decoder, 0x31C-0x374 are undocumented. | `mtk_jpeg_dec_hw.c` (415 lines); PLAN §1.4 | [V]/[I] | lead |
| 1.18 | VENC: `mtk-vcodec` (25k lines) is V4L2 glue + per-SoC power (`mt6885/mtk_vcodec_{dec,enc}_pm.c`) + IPI structs; it contains no codec-core register programming. | `K/drivers/media/platform/mtk-vcodec/` | [V] | lead |
| 1.19 | The VENC/VDEC "firmware" is the process that opens `/dev/vpud` (`VCU_PATH`); the ioctl ABI is `VCU_SET_OBJECT`/`VCU_GET_OBJECT`/`VCU_MVA_*`/`VCU_PA_*`/`VCU_CACHE_*`/`VCU_GCE_SET_CMD_FLUSH`/`VCU_GCE_WAIT_CALLBACK`/`VCU_SET_MMAP_TYPE` plus `mmap` of the register windows (`VCU_MAP_HW_REG_NUM 4`). | `K/drivers/media/platform/mtk-vcu/mtk_vcu.c`:64; `K/include/uapi/linux/mtk_vcu_controls.h`:177-190 | [V] | lead |
| 1.20 | The SCP is not involved: no vdec/venc/vcodec/vcu reference under `K/drivers/misc/mediatek/scp/`, `CONFIG_VIDEO_MEDIATEK_VPU` is not set, and there is no codec firmware image to load. | `cupida_defconfig`:3317-3321; grep of the SCP tree | [V] | lead |
| 1.21 | VDEC/VENC register semantics, the encoder RCPU microcode and rate control exist only inside `libvcodecdrv.so` (the microcode is data: `ucH264RCPUCode_*`, `ucHEVCRCPUCode_*`). | `VFS/lib/libvcodecdrv.so` exports | [V] | lead |
| 1.22 | MDP is a CMDQ relay (45k lines) and is not planned; iOS scales/converts on the Mali today. | `K/drivers/misc/mediatek/mdp/`; PLAN §1.5 | [V]/[I] | lead |
| 1.23 | `libvcodecdrv.so` build attributes: ARMv8-A, cortex-a55, ARM ISA permitted, Thumb-2, VFPv4/NEON (`Tag_CPU_arch v8-A`, `Tag_CPU_name cortex-a55`). | `llvm-readelf -A VFS/lib/libvcodecdrv.so` | [V] | hw-d01 |
| 1.24 | The VCU node `vcu@16000000` (`mediatek-vcu`, vcuid 0) exposes exactly **three** register windows: 0x16000000+0x40000 (VDEC), 0x17020000+0x10000 (VENC), 0x17820000+0x10000 (VENC core 1); `VCU_MAP_HW_REG_NUM` is 4, so one slot is unused. | `K/arch/arm64/boot/dts/mediatek/mt6893.dts`:6212-6218; `mtk_vcu.c`:133 | [V] | hw-d01 |
| 1.25 | The encoder DT node `venc@17000000` (`mediatek,mt6885-vcodec-enc`) has the two 0x2000 windows 0x17020000 and 0x17820000, GIC SPI 308, larb7, clocks VENC0/VENC1, and `mediatek,vcu = <&vcu>`. | `mt6893.dts`:5753-5771 | [V] | hw-d01 |
| 1.26 | The decoder DT node `vdec@16000000` (`mediatek,mt6885-vcodec-dec`) lists VDEC_SYS 0x1602f000, VDEC_BASE 0x16000000, VLD 0x16020000, MC 0x16021000, MV 0x16023000, MISC 0x16025000, LAT_MISC 0x16010000, LAT_VLD 0x16011000, RACING_CTRL 0x16004000, SOC_GCON 0x1600f000. | `mt6893.dts`:6281-6292 | [V] | hw-d01 |

## 2. Phone results (the only hardware evidence)

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 2.1 | M1 passed on the phone: the MT6893 JPEG encoder produced valid JFIF files from the kernel's test picture. | `runs/q10hw1/check.json` verdict "PASS: a valid JPEG of the test picture" | [V] | lead |
| 2.2 | 1920x1080 at quality code Q85: 263936 bytes, markers `E0 DB DB C0 C4 C4 C4 C4 DA`, sampling 4:2:0, PSNR Y 42.88 dB, Cb 57.13, Cr 58.92 (ffmpeg's own q3 encode of the same picture: 67099 bytes, 42.67 dB). | `runs/q10hw1/check.json` | [V] | lead |
| 2.3 | 640x480 encode took 0.73 ms; 1920x1080 took 4.6 ms (kernel cycle count in the step log). | `runs/q10hw1/09-step3.log` ("size 42240 ... CYCLE 0x677d9"), `STATUS.md` | [V] | lead |
| 2.4 | The 640x480 log line is `encode 640x480 stride 640 rows 480 q-code 0x7 src 0xd8000000 chroma 0xd804b000 dst 0xd8800000 blk 7199 ctrl 0x20100015`; done bit seen after 6 polls (736 us), `DMA 0xd880a500 DST 0xd8800000 size 42240 BLK_CNT 0x1c1f CYCLE 0x677d9`. | `runs/q10hw1/09-step3.log` | [V] | lead |
| 2.5 | On the first probe (step 0) the VEN domain was **off**: `spm status 0x10580e03`, `VEN_PWR_CON 0x1112`; after the bring-up step it was on: `status 0x105a0fff`, `VEN_PWR_CON 0xd`. | `runs/q10hw1/00-step0.log`, `04-step1.log` | [V] | lead |
| 2.6 | `CLK_CFG_13` read 0x0c030601 both before and after: venc_sel parent = 12 (`univpll_d4`), gate bit 31 clear; the driver left it as found, so no parent change was needed on this phone. | `runs/q10hw1/00-step0.log`, `04-step1.log`; parent table `clk-mt6885.c`:1221 | [V] | lead, hw-d01 |
| 2.7 | vencsys gates read 0x10000111 after step 1 (larb, venc, jpgenc, GALS on); larb7 CMD_THRT_CON went from 0x370246 to 0x300256, SW_FLAG 0 to 1, OSTDL ports 7-10 from 0x3f3f to 0x101/0x404, NON_SEC_CON bits 7-10 from 1 to 0. | `runs/q10hw1/05-step2.log` | [V] | lead |
| 2.8 | JPGENC register defaults before the first encode: RSTB 1, RST_MCU_NUM 4, IMG_SIZE 0xa200798 (left over from an earlier session), ULTRA_THRES 0xffffffff, everything else 0. | `runs/q10hw1/05-step2.log` | [V] | lead |
| 2.9 | Power-off (step 4) was acknowledged: `spm status 0x10580fff`, `VEN_PWR_CON 0x1112`, protection set again (`mm 0xc000000`, `mm2 0x3`). | `runs/q10hw1/18-step4.log` | [V] | lead |
| 2.10 | The display SMI common was only read, never written: `L1LEN 0x3 BUS_SEL 0 DCM 0x4ff`. | `runs/q10hw1/05-step2.log` | [V] | lead |
| 2.11 | The kernel image for M1 is 33525760 bytes, SHA-256 `5efacdfe...4adf15`, kernel SHA-256 `a00eca1b...291b8d`; only `sweet_touch.o` differs from q10ik. | `STATUS.md`, `build/q10hw1/.../check.log` | [V] | lead |

## 3. Imports of the vendor set

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 3.1 | The five objects (`bin/vpud` + `lib/libvpud_vcodec.so`, `libvcodecdrv.so`, `libvcodec_utility.so`, `libvcodec_oal.so`) import exactly **149** symbols from outside the set (union of undefined dynsym minus union of defined). | `python3 gpu/hwcodec/facts/imports.py` last line "149 imports" | [V] | hw-d01 |
| 3.2 | The 149 split by shim table: string/format 27, heap 8, thread 50, stdio 27, android 35, data 2. | same, `--table X` | [V] | hw-d01 |
| 3.3 | Data imports are exactly `__stack_chk_guard` and `stderr`; both must come from a data shim table or the link fails. | `facts/imports.py --table data`; hw-v03 | [V] | hw-d01, hw-v03 |
| 3.4 | `__aeabi_unwind_cpp_pr0` has no dynamic relocation (it appears only in `.ARM.exidx`), so 146 of the 149 get a named unresolved slot; the other 3 are 2 data imports and this one. | hw-v03 test output "relocations 3120, unresolved 146, unique imports 149" | [V] | hw-v03 |
| 3.5 | The set has 3120 dynamic relocations: vpud 91 (29 RELATIVE + 5 GLOB_DAT + 57 JUMP_SLOT), vpud_vcodec 191 (packed ANDROID_REL 37 + 143 JUMP_SLOT + RELR 11), vcodecdrv 2162 (packed 366 + 1401 JUMP_SLOT + RELR 395), utility 498 (packed 128 + 314 JUMP_SLOT + RELR 56), oal 178 (118 RELATIVE + 29 GLOB_DAT + 31 JUMP_SLOT). | `llvm-readelf -r -W` on each object; hw-v03 | [V] | hw-v03 |
| 3.6 | No binder, HIDL or ION call of its own: the imports are libc, `__android_log_print`, `property_get`, atrace, the ion API and the OAL (`VCodecDrv*`, `eVideo*`, `VENC_ReadHW/WriteHW/PollHW`, `eVideoGCEAddCmdWrite`, `eVideoLockHW`, `WaitISR`). | `facts/imports.py --per-object`; PLAN §1.2 | [V] | lead, hw-d01 |

## 4. Instruction census (what the interpreter must execute)

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 4.1 | **Every** defined function in the four libraries is Thumb-2: 1546 (vcodecdrv) + 677 (utility) + 80 (vpud_vcodec) + 72 (oal) = 2375 FUNC symbols, all with odd `st_value` (bit 0 = Thumb) except one size-0 alias `cacheTestNeon` in oal (0x2d64, points 0x14 before the real prologue at 0x2d78, Thumb code too). `bin/vpud` is stripped (no defined symbols) and its entry point 0x278d is Thumb. | `llvm-readelf --dyn-syms -W` (llvm-nm masks bit 0); `facts/census.py` | [V] | hw-d01, hw-v12 |
| 4.2 | `PLAN.md` §1.2 "8 of 1692 exports are Thumb entry points" is wrong: it is 100% Thumb; the "8" came from llvm-nm's display, which clears bit 0. | same | [V] | hw-d01, hw-v12 |
| 4.3 | The recursive walk found 309,511 reachable instructions, **all Thumb** (A32 count 0; the count moves by a few percent between runs because the lazy-fill budget stops at different points): the A32 classes V10-V12 are not on the vendor hot path. | `facts/census.py`; post to V10/V11/V12 | [V] | hw-d01 |
| 4.4 | Per-object reachable instruction counts: vpud 1696, vpud_vcodec 11826, vcodecdrv 260323, utility 32989, oal 2677 (this run; ±0.1% between runs because the lazy fill budget stops at different points). | `facts/census.py` | [V] | hw-d01 |
| 4.5 | Coverage of function-range code bytes (bytes inside `[entry, next entry)` minus literal pools and jump tables): vpud 94.7%, vcodecdrv 88.3%, utility 78.1%, oal 100.1%, vpud_vcodec 67.0%; of raw `.text`: 79.1/73.0/68.8/69.9/57.4%. The uncovered part is code reachable only through computed branches (function-pointer vtables) plus the 4-call lazy-fill budget per object; every undecodable address is listed by `--undecodable`. | `facts/census.py` | [V] | hw-d01 |
| 4.6 | Top 16-bit T32 mnemonics (V14's class, 215,707 instructions): ldr 52241, add 34205, mov 25060, movs 24062, str 14360, cmp 11006, b 6271, lsls 4698, cbz 4429, ldrb 4055, bne 3536, bgt 3534, adds 3450, bpl 3172, beq 2855, it 1903. | `facts/census.py --classes`; post to V14 | [V] | hw-d01 |
| 4.7 | Top 32-bit T32 data-processing mnemonics (V15, 32,166): movw 8291, add.w 6087, cmp.w 5423, mov.w 4023, movt 1830, and 676, orr.w 665, mvn 558, sub.w 464, mla 400, addw 337, bic 303. | same; post to V15 | [V] | hw-d01 |
| 4.8 | Top 32-bit T32 load/store+branch mnemonics (V16, 58,373): blx 23082 (almost all BLX imm to the PLT, i.e. mode switches), ldr.w 12421, str.w 4429, ldrb.w 3979, strd 3084, beq.w 2259, strb.w 1384, ldrd 1174, pop.w 1095, bne.w 929, push.w 884, b.w 730, bl 406, stm.w 406. | same; post to V16 | [V] | hw-d01 |
| 4.9 | VFP (V17) is small: 568 instructions, led by vldr 122, vstr 74, vmov.32 37, vmov 34, vcvt.f64.s32 23, vpop/vpush 19 each, vdiv.f64 17, vmov.f32 16, vmla.f64 12. | same; post to V17 | [V] | hw-d01 |
| 4.10 | NEON is small too: V18 157 instructions (vorr 47, vadd.i32 41, vand 11), V19 254 (vdup.32 20, vsli.64 20, vshl.u32 19), V20 1875 (vst1.32 426, vst1.64 341, vld1.64 290, vmov.i32 274, vtbl.8 101, vtbx.8 85). | same; posts to V18/V19/V20 | [V] | hw-d01 |
| 4.11 | 785 instructions were undecodable by llvm-objdump in reachable blocks (0.24%); every address is listed by `census.py --undecodable`. The 4 unclassified mnemonics are `smmul`, `uxtb16`, `aese.8`, `sha1su1.32`, `sha256su0.32` (crypto extensions + one multiply form, no V1x class claims them). | `facts/census.py --undecodable` | [V] | hw-d01 |
| 4.12 | llvm-mc 23.1.4 canonicalises `pkhtb r0,r1,r2` (no shift) to `pkhbt r0,r2,r1` (EAC1 0002), so a census by mnemonic can show pkhbt where the source wrote pkhtb. | hw-v15 on the board; `llvm-mc --show-encoding` | [V] | hw-v15 |
| 4.13 | The `it` instruction appears 1903 times and every one decoded as a valid IT (no UNPREDICTABLE flag); SETEND, CPS and HLT do not appear at all. | `facts/census.py`; post to V14 | [V] | hw-d01 |
| 4.14 | SMUSDX/SMLSDX/SMLSLDX exchange the halfwords of the second operand: product1 = Rn<15:0> * Rm<31:16>, product2 = Rn<31:16> * Rm<15:0>, and the subtract forms compute product1 - product2. | Arm A32/T32 instruction reference (SMUSD/SMLSLDX); Arm Compiler `__smusdx` documentation (dui0491) | [V] | hw-v11 |
| 4.15 | SMUSD/SMUSDX cannot overflow and do not set Q: each 16x16 product is in [-2^30, 2^30] and the extreme difference is 2^31-32768, so the result is always in (-2^31, 2^31); the Arm reference lists the Q rule only for SMUAD/SMUADX and SMLAD/SMLSD. | same Arm reference | [V] | hw-v11 |
| 4.16 | In T01's differential harness every `k_v11_*` Thumb stop is a T32 BL (`f7ffffd4`/`f001ec22`/`f7fffd24`) or the V15 PKH bug, not a V11 failure; the ARM rows have 0 mismatches. | hw-v11 run of `tests/emu/run_diff.sh` | [V] | hw-v11 |

## 5. VCU ABI (`/dev/vcu`)

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 5.1 | `/dev/vcu` is opened O_RDWR by `venc_vcodec_msg_handler` (0x11bd6, 0x11bf6) and `vdec_codec_msg_handler` (0xc528) of `libvpud_vcodec.so`; the decoder also opens `/dev/vpu` (0xc540), which has no driver in this kernel. | `facts/vcu_usage.py`; `facts/vcu_usage.md` §1 | [V] | hw-d01 |
| 5.2 | `VCU_SET_OBJECT` is `0x40587600` = `_IOW('v',0,share_obj)` with `sizeof(share_obj)` = 0x58 in the 32-bit daemon. | literal at 0x13cb4; `facts/vcu_usage.py` | [V] | hw-d01 |
| 5.3 | `VCU_PA_ALLOCATION` = `0xc0207606`, `VCU_PA_FREE` = `0xc0207607` (`_IOWR('v',6/7,mem_obj)`, size 0x20); `VCU_GCE_SET_CMD_FLUSH` = `0x40207608` (size 0x20); `VCU_GCE_WAIT_CALLBACK` = `0x40107609` (size 0x10). | literals at 0x17f3e, 0x18444, 0x18ba0, 0x18cac | [V] | hw-d01 |
| 5.4 | `VCU_GET_OBJECT` (`0xc058760a`, size 0x58) never appears as a literal: `md32_ipi_send` (0x14544) and `send_obj` (0x1459a) pass r1 from the ipi object/parameter, so the receive path builds the request at run time. | `facts/vcu_usage.py` | [V] | hw-d01 |
| 5.5 | The daemon maps the VDEC register window with `mmap(NULL, 0x40000, RW, SHARED, fd, 0x16000000)` in `vdec_drv_init_hw_info` (0x1e1ea); `mtk_vcu_mmap()` accepts `pa_start < 0x40000000` only inside `map_base[]` and only for `vcuid == 0`. | literal at 0x1e1ea; `K/.../mtk_vcu.c`:1653, :679 | [V] | hw-d01 |
| 5.6 | The other two mmaps (`eVideoMemMAP` 0x1822a, `eVideoMMAP` 0x19a40) map shared buffers with computed page-aligned offsets (MAP_SHMEM_ALLOC_BASE 0x80000000, MAP_SHMEM_MM_BASE, MAP_SHMEM_PA_BASE 0x800000000). | `mtk_vcu.c`:100-132; `facts/vcu_usage.md` §3 | [V]/[I] | hw-d01 |
| 5.6a | `libvcodecdrv.so` makes **no** ioctl and no mmap call: its only device access is `open("/proc/device-tree/chosen/atag,devinfo", O_RDONLY)` in `QueryCapability`/`eVEncDrvQueryCapability` (0x7626e, 0x778fa, 0x77f9c); all kernel access goes through the OAL in `libvcodec_utility.so`. | `facts/vcu_usage.py` | [V] | hw-d01 |
| 5.7 | `0x40044d34`/`0x40044d35` (magic 'M') are MediaTek ION custom ioctls and `0x40106600`/`0x80106602`/`0x40046601` (magic 'f') go to `/dev/mtk_sched`; neither is /dev/vcu. | `facts/vcu_usage.py` | [V] | hw-d01 |

## 6. AP <-> VCU message flow

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 6.1 | The AP->VCU encoder message ids start at `AP_IPIMSG_VENC_BASE`: AP_IPIMSG_ENC_INIT, SET_PARAM, ENCODE, DEINIT, QUERY_CAP; the VCU answers with VCU_IPIMSG_ENC_*_DONE and, separately, VCU_IPIMSG_ENC_POWER_ON/OFF/WAIT_ISR/PUT_BUFFER from `VCU_IPIMSG_VENC_SEND_BASE`. | `K/drivers/media/platform/mtk-vcodec/venc_ipi_msg.h`:28-44 | [V] | lead |
| 6.2 | The decoder ids are fixed: AP_IPIMSG_DEC_INIT 0xA000, START 0xA001, END 0xA002, DEINIT 0xA003, RESET 0xA004, SET_PARAM 0xA005, QUERY_CAP 0xA006; VCU answers 0xB000-0xB006; VCU->AP requests 0xC000 (WAITISR), 0xC001 (GET_FRAME_BUFFER), 0xC002 (PUT_FRAME_BUFFER), 0xC003/4 (LOCK/UNLOCK_CORE), 0xC005/6 (LOCK/UNLOCK_LAT). | `K/drivers/media/platform/mtk-vcodec/vdec_ipi_msg.h`:50-72 | [V] | lead |
| 6.3 | IPI ids are `IPI_VDEC_H264`, `IPI_VDEC_H265`, ... `IPI_VENC_H264`, `IPI_VENC_H265`, `IPI_MDP`, `IPI_CAMERA`; each has a VCU and an AP instance id. | `K/drivers/media/platform/mtk-vcu/mtk_vcu.h`:113+ | [V] | lead |
| 6.4 | The AP side sends a `struct share_obj` (0x58 bytes: `u32 id; u32 len; u8 share_buf[...]`) with VCU_SET_OBJECT and receives with VCU_GET_OBJECT; the payload is the IPI message struct, whose `msg_id` selects the handler. | `mtk_vcu_controls.h`:177-190; `venc_ipi_msg.h`/`vdec_ipi_msg.h` | [V]/[I] | lead, hw-d01 |
| 6.5 | A command buffer is executed through VCU_GCE_SET_CMD_FLUSH: up to 2048 {cmd, addr, data, mask} entries plus dma_offset/dma_size are replayed by the GCE; VCU_GCE_WAIT_CALLBACK blocks for the done interrupt. | `mtk_vcu_controls.h` (gce_cmds), `mtk_vcu.c` vcu_set_gce_cmd | [V] | lead |
| 6.6 | The kernel's V4L2 driver plays the AP role on the phone; in our architecture that role moves to `ap/n2ap_venc` (V41) and the daemon's receive path (V40 GET_OBJECT). | `work_packages.json` IV/V40/V41 | [I] | lead |

## 7. iOS side

| # | Fact | Evidence | Mark | Who |
|---|---|---|---|---|
| 7.1 | VideoToolbox `dlopen`s a fixed list of plug-in paths and calls a named register function: encoders `/System/Library/VideoEncoders/Slim.videoencoder` -> `SLIMEncoderRegister`; decoders `/System/Library/VideoDecoders/Slim.videodecoder` -> `SLIMRegister` (`VTLoadVideoDecoders` @0x194cc7ea4). | `gpu/video/PLAN.md` §1.2-1.3 | [V] | lead |
| 7.2 | Registration is `VTRegisterVideoDecoder(codecType, createInstance)` / `VTRegisterVideoDecoderWithInfo(codecType, info, createInstance)`; selection uses codec type, EncoderID, RequireHardwareAcceleratedVideoEncoder and rating, and `IsHardwareAccelerated` in the info is a claim the plug-in makes. | same | [V] | lead |
| 7.3 | Apple's own hardware plug-ins (AVD/H9/JPEGH1) register only when their IOKit service exists (AppleAVD/AppleAVE2Driver/AppleJPEGDriver); on this phone none exists and they must stay absent. | same | [V] | lead |
| 7.4 | On the phone the probe log shows `registered: hvc1=ok hev1=ok avc1=ok jpeg=absent`, i.e. VideoToolbox runs software decoders only and has no JPEG encoder. | `gpu/video/probe-run4-kernel.log` | [V] | lead |
| 7.5 | No instruction in CMPhoto, ImageIO, AVFoundation, AVFCapture, MediaToolbox, VideoToolbox or CMCapture builds the FourCC 0x6a706567 ("jpeg"), and no CMPhoto section contains the bytes 67 65 70 6a; so no stock photo path asks VideoToolbox for a JPEG codec. | scan for `movz #0x6567` + `movk #0x6a70` (W and X forms): 0 sites; `gpu/hwcodec/jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.6 | CMPhoto has 3 `VTCompressionSessionCreate` call sites (0x1a5d94c98, 0x1a5dad524, 0x1a5e0cb40; target 0x194cd162c, anchored at CMCapture 0x1a213f7bc) and the codec type is a caller field, not JPEG (`ldr w25,[x1,#8]` @0x1a5d94b2c). | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.7 | CMPhoto queries the IOKit service `AppleJPEGDriver` (`IOServiceMatching` @0x1a5d99058, string 0x1a5e27de9; `IOServiceGetMatchingService` @0x1a5d99068) and reads `AppleJPEGNumCores`, `AppleJPEGSupportsAppleInterchangeFormats`, `AppleJPEGSupportsMissingEOI`, `AppleJPEGSupportsRSTLogging`; 50 CMPhoto `__stubs` targets land in AppleJPEG.framework (0x1d3bdb000+). | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.8 | CMPhoto parses `PreferSoftwareCodec` / `RequireHardwareCodec` / `AllowPackedBuffers`; `RequireHardwareCodec` is parsed at 0x1a5d6dbb4, 0x1a5d8e134, 0x1a5d92480, 0x1a5dde5a4 and mapped to a codec-kind enum at 0x1a5d95910. | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.9 | ImageIO does not link VideoToolbox at all (LC_LOAD_DYLIB: CF, IOSurface, AppleJPEG, IOSurfaceAccelerator); AVFCapture has 0 `VTCompressionSessionCreate` call sites; CMCapture has exactly 1 (0x1a213f7bc, `BWVideoCompressorNode`), whose still path is `-[BWCMPhotoCompressionSession addMainImage:metadata:options:]` @0x1a1f28e94. | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.10 | VideoToolbox cstrings `EnableHardwareAcceleratedVideoEncoder` @0x194fdd28f and `RequireHardwareAcceleratedVideoEncoder` @0x194fdd2f7 are the registry filter keys. | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.11 | `-[BWIntermediateJPEGCompressor _setupJPEGCompressionOptions]` does **not** exist in the iOS 16.7.16 cache (10363 CMCapture methods, no selector containing setupJPEG/CompressionOptions); this corrects `gpu/video/PLAN.md` and PLAN §3. | `jpeg/ROUTING.md` | [V] | hw-j02 |
| 7.12 | Conclusion: a VideoToolbox `'jpeg'` plug-in serves VT jpeg clients only; Camera/AVCapturePhotoOutput and ImageIO JPEG need a CMPhoto/AppleJPEG or ImageIO hook. | `jpeg/ROUTING.md` | [I] | hw-j02 |
| 7.13 | A working software 'jpeg' VideoToolbox encoder object already exists in `gpu/video/vt/n2jpeg_vt.c` (host-tested, 345 checks, not yet on the phone); the hardware back end goes behind the same object. | `gpu/video/vt/n2jpeg_vt.c`; `STATUS.md` | [V] | lead |
| 7.14 | XNU for arm64 has no AArch32 user mode, so the vendor ARM32 libraries run under an interpreter inside one daemon (`n2vcodecd`); the daemon speaks `eVEncDrv*`/`eVDecDrv*` directly, not the IPI layer. | PLAN §4 | [I] | lead |

## 8. Open questions

| # | Question | Why it matters | Mark | Who |
|---|---|---|---|---|
| 8.1 | Does the JPEG block write complete headers for every quality code, and is `BYTE_OFFSET_MASK`/`STALL_ADDR0` handling right for buffers not 16-byte aligned? | M2/M3 buffer management | [I] | lead |
| 8.2 | Can CMPhoto be steered to a VideoToolbox 'jpeg' encoder instead of AppleJPEGDriver? | determines whether Camera "Most Compatible" uses the block | [I] | lead |
| 8.3 | Which instructions does the vendor code use that the interpreter classes still return UNDEF for? | the census covers 99.8% of reachable instructions but 785 are undecodable and 4 mnemonics are unclassified | [V]/[I] | hw-d01 |
| 8.4 | Does the GCE software executor need to emulate hardware events (CMDQ_EVENT_VDEC_LAT_*) by polling, and are the vendor's command lists bounded? | V40/IV design | [I] | lead |
| 8.5 | Are the secure-world paths (`*_sec`, `*SecureVdecCA`) needed for playback? Assumed out of scope. | VDEC scope | [I] | lead |
| 8.6 | What are the two computed mmap offsets in `eVideoMemMAP`/`eVideoMMAP` at run time? | V40 shared-memory mapping | [I] | hw-d01 |
| 8.7 | Is the interpreted rate-control path fast enough for 30 fps? | decides interpreter vs binary translation | [I] | lead |
| 8.8 | Answered (see 5.6a): `libvcodecdrv.so` has no mmap at all; the encoder register window must be mapped through the OAL's `eVideoMMAP`, so V40 only needs the register windows that the utility library asks for. | `facts/vcu_usage.py` | [V] | hw-d01 |
