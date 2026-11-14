# DEC07 — `RWVC_WIN_VDEC_ALL` (ABI rev 3): the decoder's whole register space for `vcodecd`

Package DEC07. Host-only: nothing here ran on the phone. The tree files this package changes are owned by others, so the
deliverable is **four patches** (they apply to the tree as of the run below) plus the tests that prove them.

| Patch | Applies to | Owner to apply it | What it does |
|---|---|---|---|
| `patches/redwood_vcodec-rev3.patch` | `include/redwood_vcodec.h` | hw-lead | adds `RWVC_WIN_VDEC_ALL = 12` (0x16000000 + 0x30000), `RWVC_WIN_COUNT` 13, the page → block map and the one larb write exception in the header comment; `RWVC_ABI_VERSION` stays 1 (no ioctl or struct changed) |
| `patches/nord2_vcodec-vdec-all.patch` | `kern/bsd/dev/nord2_vcodec.inc` | **integrated by hw-k02** | the window entry, the per-4 KiB-page block check in `n2vc_op_window()` **and** in the POLL revalidation, `EACCES` when that page's block is off, writes to the two kernel-owned larb pages refused except the bit-0 sleep-protect words (hw-k02 added the larb4 0x00c entry from DEC04/opus-dec2's trace). Kept for the record; `run_tests.sh` tests the tree file directly and only applies this patch to a tree without the feature. |
| `patches/n2vc_hw_fake-vdec-all.patch` | `tests/hw/n2vc_hw_fake.{c,h}` | hw-t02 | the `VDEC_ALL` window (0x30000, separate backing array), the same per-page power rule and larb read-only rule, script name `VDEC_ALL`. Needs header rev 3. |
| `patches/t_fake-vdec-all.patch` | `tests/hw/t_fake.c` | hw-t02 | two table entries so T02's generic window loop covers the new window (suite stays green). Needs header rev 3. |

All four patches apply from `gpu/hwcodec` with `patch -p1 -i kern/test_dec07/patches/<name>.patch` (they were posted
with that instruction; the driver one is already in the tree, so only the other three are pending).

## Page → block map (the ABI rev 3 text)

48 pages of 4 KiB. `VDEC_CORE` is VDE2, `VDEC_SOC` is VDE. `VDEC_CORE` holds a hidden reference on `VDEC_SOC`
(`n2vc_blocks[]`, because the core's clock path needs `MT_CG_SOC` + `MT_CG_VDEC0`), so "core page" means both domains up.

| pages | offset | block | registers |
|---|---|---|---|
| 0x00–0x0c | 0x00000–0x0cfff | VDEC_CORE | `VDEC_BASE` 0x16000000 (+0x800 UFO), the vendor's 0x16001000/0x16002000/0x16003800, `VDEC_RACING_CTRL` 0x16004000 |
| 0x0d | 0x0d000 | VDEC_SOC, **read-only except 0x00c bit 0** | larb5 (kernel-owned; 0x00c is `SMI_LARB_SLP_CON`, the vendor's sleep-protect bit, opus-dec1's LAT trace) |
| 0x0e | 0x0e000 | VDEC_CORE | larb6 (0 ports; the kernel never programs it) |
| 0x0f–0x1f | 0x0f000–0x1ffff | VDEC_SOC | `VDEC_SOC_GCON` 0x1600f000, `VDEC_LAT_MISC` 0x16010000, `VDEC_LAT_VLD` 0x16011000, the vendor's LAT sub-blocks 0x16011800–0x16018800 |
| 0x20–0x2d | 0x20000–0x2dfff | VDEC_CORE | `VDEC_VLD` 0x20000, `VDEC_MC` 0x21000, AVC_VLD 0x22000, `VDEC_MV` 0x23000, PP 0x24000, `VDEC_MISC` 0x25000–0x28fff, 0x29000 |
| 0x2e | 0x2e000 | VDEC_CORE, **read-only except 0x00c bit 0** | larb4 (kernel-owned; 0x00c is `SMI_LARB_SLP_CON`, the same sleep-protect bit, opus-dec2's CORE trace) |
| 0x2f | 0x2f000 | VDEC_CORE | `VDEC_SYS` 0x1602f000 |

**Deviation from the first proposal, on purpose:** `facts/VDEC_PROTOCOL.md` §4 said "0x00000-0x1ffff VDE". The two core
sub-blocks below 0x10000 (`VDEC_BASE`, `VDEC_RACING`) are already `VDEC_CORE` in K02's accepted per-window table, and
page 0x0e (larb6) is in the VDE2 clock domain (`scpsys SCP_SYS_VDEC2`, `mt6893.dts`:6051). Mapping those to the core is
strictly safer (core implies SOC) and cannot widen an existing allowance. Every other low page (0x0f–0x1f) stays SOC as
proposed. DEC02's measured page list can change single pages in `n2vc_vdec_page_blk()` without touching anything else.

## Tests

```sh
bash gpu/hwcodec/kern/test_dec07/run_tests.sh
# N2TEST DEC07 PASS 2370129/2370129
```

Steps (all under `/tmp/n2hw-DEC07/run`, ~1 min):

1. the four patches applied to a fresh copy of the current tree files; a stale patch fails with `patch <name> is stale`;
2. K02's own suite (`kern/test_k02/t_k02.c`, unchanged) against the patched driver, with header rev 3 **and** with the
   current rev 2 header (scaffold): `N2TEST K02 PASS 588919/588919 (violations 0)` each, ASan+UBSan and -O2 (K02's own suite now tests the new window too);
3. `t_dec07.c` against the patched driver with its **own** MT6893 model (not K02's): all 48 pages, powered/off, batch
   stop semantics, the larb allow-list (0x00c bit 0 allowed, every other larb word refused), POLL power-off during the
   sleep, the ten old windows: 569 checks;
4. T02's `t_fake.c` against the patched fake (`6194/6194`) and `t_fake_dec07.c` (463 checks);
5. staged kernel compile of `bsd/dev/sweet_touch.c` with the patched driver and header: 0 unknown symbols.

Mutation checks done while developing (each must fail, and did, against the current tree driver and the staged fake):
driver pages all mapped to CORE (64 failures), every larb write allowed (23), one page mapped to the wrong block (3),
the larb4 entry dropped (4); fake: all pages CORE (35), every larb write allowed (12), allow-list mask ignored (2), window
size 0x1000 (256).

## Not done / open

- No phone run. This is a window permission check; the first real test is `vcodecd` decoding on the phone.
- The page map is `[I]` for the pages the vendor library uses but the open sources do not name (0x01000, 0x02000,
  0x03800, 0x12000–0x17fff, 0x18800, 0x24000, 0x29000). They are covered by the conservative rule above; DEC02's
  `facts/vdec_trace/PAGES.md` is the measurement that can shrink the allowance.
- The larb allow-list has two entries, larb5 and larb4 0x00c bit 0, from opus-dec1/opus-dec2's host traces and DEC04's
  measured handshakes (`vdec_hal_wait_larb_sleep_protect` writes bit 0 and polls bit 16). If DEC02 shows more words,
  they go into `n2vc_vdec_allow[]` with their bit mask.
