# Read-only EMI permission audit — 2026-10-20

No implementation, device operation, MPU change or EMI placement was performed.

## Observations and selected source

Saved `backup/nord2/config.gz`: CONFIG_MEDIATEK_EMI=y, CONFIG_MTK_EMI unset,
CONFIG_MTK_EMI_MPU unset. `../nord2-current-kernel/drivers/memory/mediatek/Makefile:13–15`
therefore selects emimpu_old.c. That file:43–50 reads protection using structured
SMC, not raw MPU MMIO. Its :80–110 decodes SA/EA and APC. The modern emimpu.c
has the same read ABI but is not the selected saved configuration.

`dtb/nord2/live.dts:4586–4602` supplies MPU resources10226000+1000 and
10225000+1000, DRAM40000000..23fffffff inclusive, region count32, domain count16,
address alignment16, AP region31. `:1099–1105` marks CONN reservation no-map,
size450000/alignment100000/dynamic allocation range50000000..90000000; it does
not itself pin the allocated base. Saved `backup/nord2/post-probe-dmesg.txt:1874`
reports actual Android CONN base8fb00000 and MCIF8a000000. This is Android
observation, not evidence of current LK/XNU permissions or cold ownership.

`conninfra/platform/mt6893/mt6893_emi.c:37–41,88–102` programs region27 with
AP domain0, CONN2, SCP3 unrestricted under Linux. Other domains retain initialized
policy. This is policy only; do not assume it is inherited or execute it.
All conninfra paths above are under Android drivers/misc/mediatek/connectivity/.

## Exact proposed structured read ABI

`include/linux/soc/mediatek/mtk_sip_svc.h:20–31,159–160` defines fast SiP SMC64
function0x50b = x0=0xc200050b. `include/memory/mediatek/emi.h:29–38` defines
READ=2 and SA=0/EA=1/APC=2. emimpu_old.c:43–50 calls:

x0=c200050b; x1=2; x2=selector; x3=region; x4=domain-group; x5=x6=x7=0.
There is no byte-offset argument in this ABI. Minimal eight-query allowlist:
for each region27 and31, query (selector,group)=(0,0),(1,0),(2,0),(2,1).
Reject every other region/selector/group locally. Never call SET0, CLEAR1,
SLVERR3 or debug actions. No raw MMIO access is justified by these definitions.

Vendor helper returns `(unsigned int)smc_res.a0`, with no independent status.
Preserve full raw x0..x3 in any future wrapper; known SiP errors -1,-2,-3,-4
are declared at mtk_sip_svc.h:12–16. Unknown or all-ones results cannot establish
permissions. There is no observed firmware response to these queries yet.
The supported source read path does not guarantee firmware acceptance or
bounded execution; a software deadline cannot interrupt a hung SMC.

Existing XNU `osfmk/arm64/sweet_early.c:1354–1364` nord2_psci_call is a calling
pattern only: it accepts x1..3, clobbers x4..17 and truncates x0 to int32.
It cannot be reused unchanged: EMI needs x4 input, explicit zero x5..7 and raw
results. Audit clobbers/register outputs before implementing a separate wrapper.

## Decode and expectations, not measurements

Using live-DT align16 and DRAM base40000000:
PAstart=(uint64(SA)<<16)+40000000;
PAend_inclusive=(uint64(EA)<<16)+40000000+ffff.
Require valid/error-free raw fields, start<=end and range within live DRAM.
Expected CONN range if region27 is programmed:8fb00000..8ff4ffff,
SA4fb0/EA4ff4. Expected full-DRAM region31:SA0/EA1ffff. Neither is measured.

Domain d uses group=d/8 and `(APC[group]>>(3*(d%8)))&7`.
Codes:0 unrestricted;1 secureRW;2 secureRW+NSread;3 secureRW+NSwrite;
4 secureRead+NSread;5 forbidden;6 secureRead+NSreadWrite;7 NONE/reserved.
Group0 bit31 is lock (emimpu_old.c protection writer around :850).
Do not reject a valid lock bit merely because a signed32 view is negative.

Region27+31 is only the smallest diagnostic snapshot. Effective access proof
requires all32 region ranges/APCs and hardware overlap/priority semantics;
a third overlapping region can invalidate conclusions from only27/31.
Do not relax MPU protections to make an experiment proceed.

For identity reference only, old mt6885/mt_emi.h:34–40 derives raw addresses:
region27 SA1022616c,EA1022626c,APC0=1022636c,APC1=1022646c;
region31 SA1022617c,EA1022627c,APC0=1022637c,APC1=1022647c.
These are NOT a safely-readable MMIO allowlist; prefer structured supported reads.

## Existing AP and conditional WFSYS snapshots

Already hardware-tested AP-side read registers in sweet_touch.c ni_wifi_snapshot:
10006304 power(mask1f);1000616c/10006170 CONN ack(mask2);
10006504 RC(mask1);10001228 AXI protection(mask6000);
10001724 secondary protection(mask4);1020e504 GALS(mask1).
Retain existing PMIC/rail/clock/ack prerequisites before CONN access.

After those prerequisites and CONN identity20010101, source-supported conditional
reads for WFSYS state are18000010 CPUreset bit0 (0 asserted),18000000 power bit7,
180602cc power-status bit2. Source soc3_0.c:2164–2196 and soc3_0.h:112–115.
Cold CONN measurements alone do not establish these WFSYS values. No WFSYS CPU
release, power-control or reset writes are part of this read-only proposal.

## XNU aliases and placement gate

`tools/mkadt-nord2.py:13–19,29–30` advertises only142000000..180000000 RAM.
`sweet_early.c:109–121,220–241` identity tables explicitly map display14000000,
pmsg48000000,framebuffer76000000/78000000 and high RAM, not8fb00000.
Thus this allocation/identity path excludes CONN EMI, but dynamic page-table
aliases still require audit before any mapping or write. Use one consistent
mapping/cache policy; do not infer AP permission or radio-side visibility from
absence in XNU RAM. No proposed ROM placement proceeds until ownership,
permissions, cache aliases, WFSYS cold state and restoration are established.
