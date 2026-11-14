<!-- SPDX-License-Identifier: MIT -->
# R2 — status (reviewer 2: kernel, JPEG, daemon/protocol, plug-ins, rules)

Worker: `hw-r2`. Owned path: `gpu/hwcodec/review/r2/**`. Nothing outside it was edited.
No phone, no adb/fastboot, no publishing, no commits, no vendor copies. Host tests only.

Last update: 11 November 2026, 16:16 local.

## What is implemented

- `review/r2/FINDINGS.md` — verdicts for every package of tracks K, J, P, D and detailed findings
  with file:line evidence, Linux/XNU oracle citations and a rule-compliance sweep.
- `review/r2/STATUS.md` — this file.

I wrote no product code: the package is a review. Findings were posted to the owners (K02/IK via
the opus-kern audit thread, IP, D01, hw-lead) and are summarised here.

## Verdict summary (details in FINDINGS.md)

| pkg | verdict |
|---|---|
| K01 | accept with findings (no DONE post; NV12 path sound, YUYV chroma address F9) |
| K02 | **accept rev3 (a939f107)** — F1-F4 and the opus-kern HIGH-1/2/3/MED-4 are fixed and tested (588871/588871); hand-off waits only for IK to regenerate 0299 from this sha and compile_check 0 unknown |
| IK | **accept** (final relay a2e90ed5): 1479/1479 x2, 0299 compile 0 unknown, 4439/4439; F7/F8 low |
| J02 | accept with findings (low only) |
| J04 | accept with findings (DONE): 5296/5296; 11 words [V], 9+4 hypotheses, no hardware proof (F30/F31) |
| IP | accept with F15 build blocker: F14 fixed (722/722); F15 method fixed (IV strong tables + object extraction), link needs 3 sdk_symbols.txt entries |
| P03 | **accept** (DONE rev 2): 550/550; F26-F29 all fixed |
| P04 | accept with findings (DONE rev1): 5031/5031; F32 (-12915 [I]) and F33 (hook/build) open, documented |
| D01 | accept with findings (F22 infracfg offsets, F23 count, F24 marks, F25 4:2:2 stride) |

## Tested (host, exact last lines)

    $ bash gpu/hwcodec/kern/test_k01/run_tests.sh
    N2TEST K01 PASS 822/822 (violations 0)        (three builds: ASan+UBSan, -O2, TSan)

    $ bash gpu/hwcodec/kern/test_k02/run_tests.sh
    N2TEST K02 PASS 588871/588871 (violations 0)  (two builds, rev3 a939f107)

    $ bash gpu/hwcodec/kern/test_ik/run_tests.sh
    N2TEST IK PASS 1479/1479 x2
    0299: files 6 lines 2730 bytes 117119; KERNEL COMPILE OK, new undefined symbols: 0 unknown
    N2TEST IK PASS 4439/4439

    $ bash gpu/hwcodec/jpeg/test_j02/run_tests.sh
    N2TEST J02 PASS 1483/1483

    $ bash gpu/hwcodec/daemon/test/run_tests.sh
    N2TEST IP PASS 722/722        (current tree)

    $ bash gpu/hwcodec/client/test/run_tests.sh
    N2TEST P03 PASS 550/550

    $ bash gpu/hwcodec/jdec/run_tests.sh
    N2TEST J04 PASS 5296/5296

    $ bash gpu/hwcodec/vt/test/run_tests.sh
    N2TEST P04 PASS 5031/5031

    $ bash gpu/hwcodec/facts/run_tests.sh
    N2TEST D01 PASS 32/32

Note on the IK line: the current `test_ik` failure is in the host stand-in file (`t_relay.c`),
which IK is editing for the new `proc_find`/`n2vr_reap` code; the kernel relay source itself is not
the compiler error. The last green suite is the DONE rev3 one quoted above.

## Rule compliance (16:10 sweep)

- No `.o/.a/.so/.dylib` and no vendor libraries in worker-owned dirs; no file >300 kB in
  `kern/jpeg/daemon/client/vt/install/review`; every package dir <2 MB.
- `find gpu/hwcodec -type f -size +300k` hits only `build/**`, `dsh-state/logs/**` and
  `runs/q10hw1/kernel.log` (harness/phone artifacts, outside package sources; reported to hw-lead).
- Missing SPDX (line 1 or 2) only in IK's `compile_check.sh`/`make_patch.sh`/`test_ik/run_tests.sh`
  and K01's `test_k01/run_tests.sh`; K02 rev4/J02/IP/P03 scripts carry it. T01's line-2 scripts are fine.
- `git status` changes outside `gpu/hwcodec` are the video/GPU sessions' work; `emu/n2a32_t16.c`
  is T01's announced application of R1's V14 patch. No unannounced cross-package edit found.
- 0299 is current (e04aa289) and verified to embed the final K02/IK/JPEG files; compile 0 unknown.

## Unresolved / next

1. K02 rev3 accepted; only IK's regenerated 0299 + `compile_check.sh` 0 unknown remain for hand-off.
2. IK final relay accepted; F7/F8 are low. DONE rev4 is for the record only.
3. IP: F14 verified; F15 method fixed. Add `_pthread_key_create`, `_pthread_setspecific`, `_strnlen` to `daemon/sdk_symbols.txt`, re-run `build_ios.sh`, and assert the `N2VHOST tables` line has non-zero thread/stdio.
4. Review P03 (`client/**`) and P04 (`vt/**`) when they post DONE; P03's header is already read.
5. J04 has no code; review when it lands.
6. FACTS.md F22 (infracfg offsets) is a documentation hazard for the next integrator; posted to
   hw-d01.

## Files changed

- `gpu/hwcodec/review/r2/FINDINGS.md` (new)
- `gpu/hwcodec/review/r2/STATUS.md` (new)
- `gpu/hwcodec/review/r2/run_tests.sh` (new; self-check of the review package)

## Review-package self-check

    $ bash gpu/hwcodec/review/r2/run_tests.sh
    N2TEST R2 PASS 27/27
