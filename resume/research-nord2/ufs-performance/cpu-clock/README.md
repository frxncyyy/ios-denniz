# Isolated MT6893 configured-clock telemetry

Pure decoder and caller-injected read adapter only. No active kernel changes,
MMIO execution, mapping, device operations or runtime hook. Run `python3
research/nord2/ufs-performance/cpu-clock/test.py` for host ASan/UBSan and local
freestanding AArch64 compilation.

Selected source is ../nord2-current-kernel, MT6893 implementation:
- Saved kernel config MACH_MT6893=y selects cpufreq_v1/src/mach/mt6893 via
  src/mach/Makefile3–5, despite generic MTK_PLATFORM stringmt6885.
- drivers/misc/mediatek/base/power/cpufreq_v1/src/mach/mt6893/
  mtk_cpufreq_platform.c32–48 defines the exact ten addresses;292–309 maps
  LL to20c/a2a0, L to22c/a2a4, B to21c/a2b0.
- :325–352 broadcasts L/B programming to23c/24c/21c mirrors;
  :366–370 and516–522 broadcasts divider/mux to a2a8/a2ac/a2b0.
  Snapshot pairs2/3 are mirror register pairs; no independent CPU identity is
  assigned. Five pairs are LL,L,mirror1,mirror2,B in that order.
- :412–496 defines exact integer rounding and divider fractions; :499–509
  reads CON1 plus divider; :526–529 reads mux10:9.
- mtk_cpufreq_config.h65–71 defines mux0CLKSQ,1ARMPLL,2MAINPLL,3UNIVPLL.

Allowlist order: five PLL CON1 words1000c20c,1000c22c,1000c23c,1000c24c,
1000c21c, then five divider/mux words0c53a2a0,a2a4,a2a8,a2ac,a2b0.
These are ten single32bit reads per pass, three full passes (30calls maximum).
Any read error stops immediately; failing callback output is discarded, raw
status retained, unread values have no valid bit. No implicit retry or alternate
address is possible. Exactly matching three full raw snapshots are required
before decoding any pair. A->B->A samples fail; changes between identical
snapshots remain unobservable. This is not an atomic DVFS snapshot guarantee.

Decoder retains raw values, PCW21:0, POSDIV26:24, divider21:17 and mux10:9.
Arithmetic preserves the selected vendor's rounding order using64bit
intermediates: (((PCW*26)>>14)*1000)/postdivider, then divider ratio.
Known divider encodings8/16/24=1;9/10/11=3/4,2/4,1/4;
17..20=4/5..1/5;25..29=5/6..1/6. Other codes are explicitly unknown,
although Linux's default branch leaves unity. Non-ARMPLL mux, zeroPCW and
unknown encodings never get a valid configured_khz result.

Conservative POSDIV policy: source explicitly decodes0/1/2/3 as/1,/2,/4,/8.
Its default branch treats ALL4..7 as/16, but does not identify which default
codes are meaningful hardware encodings (setter343–345 only selects0..2).
This helper preserves4..7 raw and returns UNKNOWN_POSTDIV rather than upgrading
that default into supported hardware knowledge. All300 supported combinations
are compared to the EXACT extracted vendor pure math, not a duplicate formula.
If a separately established hardware encoding4=/16 is desired later, review
that evidence before widening support. No current hardware observation exists.

Configured rates are model telemetry, never physicalMHz, PLL lock/enable proof,
effective CPU rate or throughput measurement. CON1/divider words do not expose
all clock gating, power and execution-state conditions. Three stable snapshots
cannot establish a boot-wide rate, nor explain boot wall time by themselves.

Future native adapter plan (not implemented/activated): independently approve
AP mapping/access against liveDT APMIXED1000c000+e00 and MCUCFG0c530000+10000,
then expose only this exact physical-address allowlist through a32bit volatile
read callback. Preserve callback order and no hidden enable/map-changing writes.
No PLL/DVFS/FMETER/PMU/clock-vote/regulator writes, counter enabling or reset.
Mapping/access safety and inherited permissions remain unproven. A read may
fault/hang; a host deadline or software iteration cap cannot bound a hung MMIO
transaction. Integration needs its own ownership/mapping/fault-path review and
explicit runtime opt-in; nothing here automatically activates later.

## Native integration candidate (still PRIVATE)

`native_adapter.inc` is included only in the staged `sweet_ufs.c` copy matching
CI39503015652/8702715 source; original source SHA is in integration-base.json,
and native.patch shows the three insertion points. Explicit nord2cpuclock=1 AND
nord2ufsprofile=1 are required. Init maps the two source-backed controller
ranges and collects pre-root before the UFS mutex; first GUI captures after the
existing report unlock. No per-I/O hook, timer, worker, retry or hot-path change.

Each sample prints13bounded lines: stage/status/configured_only/attempts and
actual CNTFRQ; three validity masks; six raw rows; five decoded pairs. Read
failure adds one error row with exact pass/word/result. Stable=-1 means capture
incomplete,0 means mismatch,1 means three equal full snapshots. Pair status
must equal N2CLK_OK before configured_khz is interpreted. Absent pair data has
BAD_ARGUMENT status, not valid zero-rate telemetry. Numeric status enum is in
configured_clock.h. No output is inside the UFS mutex. Permanent controller
mappings are confined to opt-in mode; failed mapping stops reads with explicit
error instead of attempting direct physical access.

`test-native.py` compiles the EXACT adapter with injected mappings and boot args,
checks opt-out/no map/no output, both map failures, explicit allowlist rejection,
pre-root/GUI13-line reports, unknown mux, ASan/UBSan. `compile-integration.py`
compiles the entire staged driver with captured CI flags adapted to local paths,
active sweet_early.h and generated headers; compile-command.json/compile.log
and sweet_ufs.o are its evidence. No active source was changed or built by it.
Native mapping/access safety still needs separate review before integrating or
running an image. No physical clock-frequency measurement is claimed.
