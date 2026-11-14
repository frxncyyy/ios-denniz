# opus-perf: interpreter speed and vendor encode load (STATUS)

Host only. Nothing ran on the phone, nothing was published. Build products are in `/tmp/n2hw-perf`.

## Answer first

The interpreter is not the bottleneck for 1080p30 H.264. One encoded frame costs the vendor stack about
**11-13 thousand guest instructions** of codec work (libvcodecdrv + libvcodec_utility + libvpud_vcodec).
The count is the same at 320x240 and 1920x1080: rate control runs on the VENC's RCPU, and the CPU only
does bookkeeping, GCE command assembly and register writes. On top of that, about 65 host calls per frame
(`__android_log_print`, `ioctl`, `malloc`/`free`, mutexes) run natively.

At the measured 15-80 guest MIPS that is 0.15-0.8 ms per frame, against a 33 ms budget.

The real CPU risk is somewhere else. vpud has a log thread that busy-polls `GET_LOG_OBJECT` on /dev/vcu.
The V40 model answers at once, so this thread loops forever (`strstr` x14, `malloc`/`free`, one
`__android_log_print` "[Invalid debug cmd]" per turn). Over the session it used ~1.9 G guest instructions
and wrote a 136 MB log. On the phone that is one core at 100% plus a console log flood, unless the model
blocks the way the kernel does.

## Vendor run: guest instructions per phase

`bash gpu/hwcodec/tests/perf/run_vmix.sh --tree <tree> [--size WxH]` counts every guest thread. It was
run on opus-venc's /tmp tree (`/tmp/n2hw-venc/src/gpu/hwcodec`, fake VENC, SEQ_HDR + 6 frames,
bitstream validated by T02's checker). The plain tree stops at M-d, before ENC_INIT_DONE.

"without vpud" leaves out vpud's own code, which is its log-poll loop and service plumbing. The poll
part depends on wall time.

| phase | 320x240 total | 320x240 without vpud | 1080p total | 1080p without vpud |
|---|---:|---:|---:|---:|
| vpud start-up | 7.0 M | 1 822 | 6.1 M | 1 822 |
| ENC_INIT (venc_open) | 1.47 M | 935 359 (98% `CirQueue_Create` init loop) | 1.38 M | 935 425 |
| SET_PARAM (start) | 23 296 | 6 486 | 113 378 | 6 444 |
| SEQ_HDR | 20 765 | 4 241 | 19 880 | 4 695 |
| frame 1 (IDR) | 28 706 | 10 453 | 72 211 | 10 528 |
| frames 2..6 / 2..4 | 25-37 K | 10 923-12 983 | 26-35 K | 10 938-11 979 |
| FRAME_FINAL | 8 142 | 1 458 | 28 435 | 1 573 |
| close + idle (poll loop) | 662 M | 5 709 | 1 872 M | 5 663 |

**Per-frame mix (1080p, frame 3).**
- By instruction set: T16 46%, A32 29% (the PLT stubs `add r12,pc` / `ldr pc,[r12,#]!` are ARM), T32 25%.
- By kind: integer 56%, load/store 27%, branch 17%, VFP 0.2%, NEON 0.1%.
- Top functions: `VENC_WriteHW`, `eVideoGCEAddCmdWrite`, `H264_SeqLvlMMRSetting`, `H264_EncodeFrameStart`,
  `fill_vsi_info`, `H264_EncodeFrameEnc_ReadResult`.
- No software CABAC/CAVLC slice coding and no NEON colour conversion runs. A static scan finds NEON almost
  only in decoder functions and struct copies.
- About 65% of executed guest instructions take the new fast path.

## Interpreter speed (micro benchmark)

`bash gpu/hwcodec/tests/perf/run_bench.sh [--emu DIR] [--prof]` runs six guest kernels: ALU, word copy +
struct RMW, insertion sort/switch, Exp-Golomb bit writer, float rate-control math, and a NEON u8 loop. They
are built for A32 and T32 at -O2 and run through V03.

The script reports wall ns, host instructions and cycles per guest instruction (perf_event, user only).
The host is an i5-10210U capped at 1.9 GHz on a shared, loaded laptop. Cycles per instruction is the
stable figure. Min of 3 runs:

| kernel set | before: ns / host insn / cycles | after: ns / host insn / cycles | gain in cycles |
|---|---|---|---|
| T32 integer (alu, mem, branch, bitw) | 19.7-23.4 / 147-154 / 49-53 | 12.6-16.0 / 96-118 / 29-37 | 1.4-1.75x |
| T32 all six (total) | 29.1 / 178 / 70.7 | 19.9 / 123 / 44.3 | 1.6x |
| A32 all six (total) | 39.7 / 224 / 96.8 | 36.8 / 215 / 86.6 | 1.1x (cache only, no A32 fast paths) |
| VFP kernel | ~600 cycles/insn | ~600 cycles/insn | (V17 calls fesetround/feclearexcept/fesetexceptflag per op) |
| NEON kernel | 230 cycles/insn | 188-217 cycles/insn | |

That is about 50-80 guest MIPS on this host for Thumb integer code, and 25-35 MIPS for the whole mix.

**arm64 estimate (conservative).** On the Nord 2's Cortex-A78 cores (2.6-3.0 GHz), assume the same
cycles per instruction as this host: about 30-45 cycles per Thumb instruction, so 60-90 MIPS. On an A55
little core assume 3x the cycles: about 15-25 MIPS. Even the worst case keeps a frame under 1 ms of
interpretation. The arm64 compile check passes: `make -C gpu/hwcodec/host ios OUT=/tmp/n2hw-perf/ios` gives
"33 objects compiled for arm64-apple-ios16.0, 0 failed". Nothing was timed on arm64.

## What changed in the core (emu/n2a32_core.c, new emu/n2a32_fast.h)

- **Per-thread decode cache.** 4096 direct-mapped entries keyed by pc and the Thumb bit. Each entry holds
  the routing result plus the predecoded operands. Each entry also stores the raw encoding, which is
  re-fetched and compared on every use. So the cache never needs invalidation: self-modifying code, a
  remapped library or a second address space simply miss.
- **Fast paths for about 70 common T16/T32 forms.** These cover ALU immediate/register, flag-setting
  arithmetic, loads/stores (imm, reg, literal, SP, LDRD/STRD), PUSH/POP(.W), B/B<c>/CBZ/BL/BLX/BX/POP
  {pc}, MOVW and T32 modified-immediate data processing. They run only outside IT blocks. Excluded on
  purpose: every other form, writes to r15 other than the listed branches, UNPREDICTABLE combinations,
  and register-specified shifts.
- **Lean Thumb inner loop.** It does the same per-instruction checks (stop_request, ilimit, special pages)
  and state updates, without the condition/IT/trace work. The run loop moved out of the function that
  calls `sigsetjmp`, which no longer pessimises it.
- **Lock-free host-call slot lookup** (publish-once slots with acquire/release).
- **Nested-call failure propagation (hw-lead request, R1 F20).** If a shim's nested `n2a32_call` stops and
  the shim did not set `stop_request`, the outer run stops with the inner reason, address and encoding. If
  the shim handled it by setting `stop_request` (V32's pthread_once), it stays STOP_REQUEST. `stop_request`
  is read atomically (F8).
- **Non-ABI switch.** `volatile int n2a32_core_fastpath` (1 = on) lets tests compare against the class
  path. include/n2a32.h is unchanged.
- **V14 bug fixed in emu/n2a32_t16.c (2 lines).** T16 `BLX rm` wrote LR before reading Rm, so `blx lr`
  branched to its own return address. ARM ARM: target = R[m] first. t_fast.c found it.
- **emu/test_v02/t_core.c.** The routing test replaces the class functions with recorders, so it now runs
  with `n2a32_core_fastpath = 0`. A new test checks that a host call whose nested call hits UDF stops the
  outer run with STOP_UNDEF at the inner address. That test fails on the old core (414/417).

## Tests (host)

    bash gpu/hwcodec/tests/perf/run_tests.sh   -> N2TEST PERF PASS 2336354/2336354
      (t_fast.c, ASan+UBSan -O1 and -O2: every T16 encoding x random states, 1.5 M random/targeted T32,
       100 k A32, single-stepped with fast paths on vs off: registers, flags, IT, D regs, FPSCR, stop,
       icount, memory identical; every fast op covered)
    package suites after the change (last lines):
      V02 868/868  V10 220589/220589  V11 960/960  V12 200797/200797  V15 1764/1764  V16 3284977/3284977
      V17 2264/2264  V18-VENDOR PASS  V19 44435/44435  V20 436882/436882  T01 5/5  R1 spot 308/308
      V03 12971/12971  V30 277633/277633  V32 13149/13149  V33 341/341  V34 2044/2044  V40 1377/1377
      V14 FAIL 449/453 before and after (lsrreg_0/asrreg_0/movpc/decode_census: pre-existing, class-level)
      IV (tsan build) and V41 fail on ap/n2ap_venc.c as edited by another worker at 17:01 (unused
      ven_sleep_ms; VENC_C1 refcount): V41's test does not run the interpreter.
    tests/emu/run_diff.sh (T01 differential) -> N2TEST T01 PASS 19180/19180

## Tools (all build into /tmp/n2hw-perf)

- `run_bench.sh` + `bench.c` + `bench_kernels.c`: MIPS, host instructions and cycles per guest instruction.
  `--prof` samples host PCs. `--emu DIR` benchmarks a saved copy of emu/.
- `n2perf_mix.c` (links instead of emu/n2a32_trace.c), `mix_report.py` and `run_vmix.sh`: guest
  instruction mix and counts per phase for any runner that attaches a trace. `run_vmix.sh` patches only a
  /tmp copy.
- `t_fast.c` + `run_tests.sh`: the differential test.

## Risks and open items

1. **vpud log-poll busy loop (high, CPU/thermal).** V40's `GET_LOG_OBJECT` returns immediately, so a
   guest thread spins and logs "[Invalid debug cmd]" every turn. Fix it in V40: block or sleep like the
   kernel, or return the "no log" answer vpud waits on. Posted to hw-v40/hw-iv.
2. **Trace always attached (high, perf).** host/n2vhost.c guest_main() calls `n2a32_trace_attach()`
   unconditionally. In library mode (vcodecd) the main guest thread therefore formats a T01 trace record
   per instruction and gets none of the fast paths. Posted to hw-iv.
3. **Logging volume.** About 65 `__android_log_print` per frame plus the poll loop's log. The shim's
   output path decides the cost; on the phone, route it to a rate-limited log.
4. **VFP cost.** V17 changes the host FP environment (`fesetround`/`feclearexcept`/`fesetexceptflag`) on
   every VFP instruction, about 600 cycles each. That is irrelevant for this encoder (0.2% VFP), but would
   matter for float-heavy code. Fix: cache the current rounding mode and set it only on change.
5. **Self-modifying code.** None expected. The vendor objects import no mprotect, cacheflush or syscall;
   text is mapped read-only by V03, and mmap64 is only used for /dev/vcu and ion. dlopen/dlsym load the
   encoder object by name. Even SMC would be correct: every cache hit re-checks the fetched word.
6. **RCPU microcode.** `ucH264RCPUCode_*` / `ucHEVCRCPUCode_*` are 32 KiB arrays in libvcodecdrv .data
   (7 H.264 and 14 HEVC variants). They are copied per session into a buffer the VENC's RCPU executes.
   That is a functional/DMA-coherency issue for the phone (the buffer must be real physical memory, flushed),
   not a CPU cost.
7. **Counts are from the fake VENC.** The per-frame numbers come from opus-venc's fake VENC and the /tmp
   V40/V41/V34 fixes. A real IRQ or error path (e.g. ReadResult retries, bitstream-full handling) could add
   work. It stays in the same order unless it loops on hardware status. Re-run `run_vmix.sh` on the phone
   tree once the fixes land.
8. **Contract.** The frozen contract says the core calls the class function. With fast paths, common forms
   are executed by the core with semantics proven identical to the real classes (t_fast.c). Only tests
   that substitute class functions can see a difference; they set `n2a32_core_fastpath = 0`.
