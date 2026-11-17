# UFS caller-cost and CPU-clock follow-up — isolated proposal

No active kernel edits, commits, device operations or clock changes. Source
candidate `sweet_ufs.c` and `candidate.patch` are against the private base/hash
in base.json. They are not part of CI39499794889/c25b788 or the current image.

## Measured facts and omitted costs

research/nord2/rtc-ufs-wifi/hardware-live/xnu-live.log:2620–2621:
51,344 READs,2,429,644,800 read bytes =>47,320.91bytes per successful READ on
average (not request-size distribution). Command9.659704s, doorbell9.215154s,
difference0.444550s; GUI ownership63.790300s and framebuffer4.827037s.
Command timing surrounds ufs_scsi_impl and includes descriptor initialization,
DMA maintenance and response checking. It excludes buf_map, waiting for the
strategy mutex and bounce memcpy. Concurrent waiter times overlap; none of
these sums can simply be subtracted from boot wall time. First GUI ownership
is not lockscreen completion. Read-byte total is traffic, not unique file data.

Current strategy (sweet_ufs.c sweet_ufs_strategy) maps caller memory, then holds
one mutex across all1MiB chunks, command completion and copies. Actual transfer
is rounded4KiB; no unconditional1MiB clearing/flushing exists. Each read copies
requested W bytes from bounce, implying at least W read+W write CPU traffic,
without a measured bandwidth/time estimate. Partial writes preread the whole
chunk when either physical4KiB edge is unaligned. Raw character physio uses
minphys64KiB (bsd/arm/param.h:70;kern_physio.c:252), but this does NOT establish
an HFS/VFS block-strategy64KiB limit. VFS VNOP_STRATEGY is synchronous here:
even async caller flags return only after this driver completes the command.
One slot and shared bounce prevent overlap; fixing that is an ownership/queue
redesign, not a small copy/barrier deletion.

## Minimal candidate instrumentation

Reuses nord2ufsprofile=1, no per-I/O logs/new lock/slot/link/PHY/DMA changes.
Mapped nonzero buffers record counts/readcounts/requested bytes,8size bins,
RAW/CLUSTER/PAGEIO/ASYNC flags and RMW command count. Local timestamps bracket
buf_map, mutex acquisition, total lock hold and each memcpy; updates occur
under the existing lock. Opt-out reads no counter. Early bound/map failures
are intentionally outside these mapped-buffer counts. Four bounded report
lines snapshot at the same existing first-GUI report, after releasing the lock.

Interpretation: compare real command-size/caller histograms with requested
buffer sizes to expose chunking/rounding; compare held vs aggregate commands
and copy time to identify CPU cost. Report waiter sum AND max; sums overlap
across callers and must not be presented as boot wall. No performance gain is
claimed. Counters/timestamp overhead also needs comparison on unchanged input.

## Exact ioctl/VFS finding — avoid accidental tuning

bsd/sys/disk.h:224 specifies DKIOCGETFEATURES uint32. Zero advertises NO FUA,
barrier, priority or unmap (bits in :87–89,235). Candidate returns exactly4zero
bytes with no additional capabilities. Existing root driver returned ENOTTY.

vfs_subr.c:4318 requires FEATURES before its remaining limit queries. With
FEATURES0, next unimplemented DKIOCGETMAXBYTECOUNTREAD still returns ENOTTY;
other missing mandatory queries are MAXBYTECOUNTWRITE,
MAXSEGMENTBYTECOUNTREAD/WRITE, MINSEGMENTALIGNMENTBYTECOUNT and COMMANDPOOLSIZE.
Existing MAXBLOCKCOUNTREAD/WRITE and SEGMENTCOUNTREAD/WRITE handlers exist but
constraints are applied only after ALL required queries succeed (:4373ff).
Features-only therefore is a narrow semantic correction, not a complete limit
implementation or request-size optimization.

Actual selected VFS code sets maxread/maxwrite64MiB before these queries;
early failure leaves32segment/32queue defaults. The exact query-sequence host
test confirms those NONZERO values and actual cluster_max_io_size1MiB from
32*16KiB then the1MiB minimum (vfs_cluster.c:331–363). No hardware invocation
or observed mount dump is claimed. spec_vnops.c:782 directly propagates the
block-driver ioctl error; it does not synthesize these missing results.

DO NOT blindly implement the remaining set using old segment_count1. Caller
buffers may consist of multiple physical pages: buf_map makes them contiguous
in CPU virtual address space and memcpy handles their contents. Internal DMA
PRDT/bounce contiguity is unrelated to caller segment limits. Applying1 could
change/restrict requests in other callers even though cluster's1MiB floor
hides it in that one function. Full limit advertisement needs a separate
caller-contract audit and measured request-size comparison. Similarly an
honest command pool1 still becomes mount queue32 minimum (:4443–4450); do not
claim it makes VFS concurrency1. This proposal leaves all those limits unchanged.

## CPU frequency: declarations versus observations

mkadt-nord2.py:109 declares2.0GHz for A55 and2.6GHz for A78. It performs no
measurement. pexpert/arm/pe_identify_machine.c:147–158 copies the CPU DT property
into gPEClockFrequencyInfo; that printed frequency is not PLL telemetry.
CNTFRQ13MHz/CNTVCT are architectural timebase, also not a CPU DVFS frequency.

Saved config.gz has MACH_MT6893=y despite MTK_PLATFORM=mt6885. Select
cpufreq_v1/src/mach/Makefile:3–5 ->mt6893, NOT mt6885! The platform's
mtk_cpufreq_platform.c:32–50,292–309,466–509 implements get_cur_phy_freq using
ordinary AP register reads (cpufreq_internal.h:180), without writes in that
read routine. LiveDT:3248 confirms APMIXED1000c000+e00; :4199 confirms
MCUCFG0c530000+10000. Read-only candidate allowlist,10words:
- PLL CON1:1000c20c(LL),1000c22c(L),1000c23c/24c(mirrors),1000c21c(B).
- Divider/mux:0c53a2a0,a2a4,a2a8,a2ac,a2b0.

Use stable repeated snapshots of all raw words, not a single torn pair. Selected
get_cur_phy_freq uses CON1 PCWbits21:0, POSDIVbits26:24, dividerbits21:17.
Its Linux-decoded kHz is (((PCW*26)>>14)*1000)/postdivider, then divider factors
from pll_to_clk(:412–463):8/16/24=1;9=3/4;10=1/2;11=1/4;17..20=4/5..1/5;
25..29=5/6..1/6. Do64bit arithmetic and preserve raw/valid flags; unknown
encodings must not silently become unity. Critically, read muxbits10:9 too:
_get_cpu_clock_switch(:526–529), config.h:65ff says1=ARMPLL,0=CLKSQ,
2=MAINPLL,3=UNIVPLL. Do not label ARMPLL-derived output as effective CPU rate
when selected source differs. No frequency-meter selector/enable writes,
clock voting, PLL/DVFS writes or regulator changes are proposed.

This is configured-clock telemetry supported by vendor source, not an oscillator
measurement or guaranteed execution throughput. Current XNU AP access to these
registers has not been tested; do not assume inherited permission or execute
reads until separately reviewed/authorized. Reads can fault/hang regardless of
software deadline. Core/domain mapping follows platform cpu_id0/4/7 (:74–105),
not DT's simplified common cluster-id; snapshot all five pairs without assuming
Linux's mirrored programming remains inherited.

An optional real-cycle estimate would require an already-enabled accessible
PMU, per-core pinning, PMCR enable/divider/width/filter knowledge and stable
CNTVCT deltas. Android arch/arm64/kernel/perf_event.c:501,553–566 reads PMCR/
PMCCNTR, but enables/resets counters elsewhere (:784,990). perf_event.h:29–35
shows enable,64x divider and64bit width bits. No existing enabled/access-safe
PMU state is proven here, so no new PMU access or counter enabling is proposed.
A loop timed only with CNTVCT measures workload throughput, not MHz.

## Verification

`test-ioattrs.py`: exact candidate ioctl, uint32 canaries, actual selected VFS
query excerpt and cluster_max_io_size; confirms next missing query and nonzero
retained defaults. `test-strategy.py`: exact candidate strategy with virtual
media, multi-chunk reads, partial-write edge preservation, timestamp/flag/
histogram/error/profile-off behavior and report lengths. Both ASan/UBSan PASS.
Whole isolated UFS source compiles with local ARM checker/active header. No CI
or device result and no performance improvement is claimed.

## Offline strategy report parser

`python3 research/nord2/ufs-performance/parse-result.py CAPTURE.log` emits JSON
and exits nonzero for unknown/invalid evidence. It requires exactly one complete
LF-terminated STRATEGY, PHASES, CALLERS, SIZES and PROFILE row (CRLF accepted),
including all expected fields; duplicate, clipped, interleaved and overflowing
rows fail closed. Raw text, line-numbered report evidence and input SHA256 are
retained even on failure. Select one capture/report before parsing repeated boots.
No hardware counter capture for this isolated candidate exists yet.

Durations use that capture's nonzero PROFILE.freq, never an assumed13MHz.
Histogram sum must equal mapped-buffer count; requested bytes must lie within
weighted nonoverlapping bin bounds (last upper bound UINT32_MAX, matching caller
buffer count). Reads and each independently overlapping flag count cannot exceed
buffers. Copy cannot exceed held time, wait max cannot exceed wait sum, and RMW
cannot appear with zero write buffers. Requested bytes are deliberately NOT
compared for equality to device traffic. Errors/timeouts remain raw counters and
do not imply capture invalidity if the report itself is consistent.

Wait totals explicitly represent overlapping caller waits, with no wall-time
savings inference. Command duration is contextual and may include work outside
strategy; it is not subtracted from held time. `test-parser.py` checks the schema
against this isolated C candidate, actual-frequency conversion, histogram/byte
bounds, flag overlap and strict missing/duplicate/truncation/overflow rejection.
