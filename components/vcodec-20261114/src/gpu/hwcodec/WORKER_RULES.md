# gpu/hwcodec — rules for workers (read all of it before the first edit)

You are one of about 30 workers building hardware video/JPEG codec support for iOS 16.7 on a OnePlus Nord 2 (MediaTek MT6893).
Read, in this order: this file, `gpu/hwcodec/PLAN.md`, `gpu/hwcodec/STATUS.md`, your entry in `gpu/hwcodec/work_packages.json`, every header
your entry names in `gpu/hwcodec/include/`, then `gpu/hwcodec/FACTS.md`.

## 1. Files
- Edit **only** the files/directories listed under your `owned_files`. New files only inside your own directories. No two packages
  share a file; if you need a change in someone else's file or in `include/*.h`, post the exact change on the board to its owner.
- `gpu/hwcodec/include/*.h` are **frozen**, owned by `hw-lead`. A defect in a header is posted to `hw-lead` with the proposed text; do not
  work around it silently and do not copy a header into your directory.
- Never touch: `gpu/video/**`, `gpu/build/**`, `gpu/hwcodec/kernel/**`, `gpu/hwcodec/build/**`, `gpu/hwcodec/runs/**`, `xnu/**`, git history,
  anything outside the work tree. Reading is allowed everywhere.

## 2. Disk and CPU (hard limits: the shared disk has about 1 GB free)
- Every build product, object, binary, test output, extracted or generated file goes under **`/tmp/n2hw-<your id>/`** (your sandbox's
  private RAM-backed tmp). Keep it **below 100 MB**; delete what you no longer need. `run_tests.sh` builds there by default.
- In the work tree you write **sources and small text only**: no binaries, no objects, no `.o/.a/.so/.dylib`, no dumps, no logs
  over 200 KB, no copies of vendor libraries, no disassembly listings over 200 KB (keep the script that makes them instead).
- Before finishing: `du -sh` of your owned directories must be under 2 MB (5 MB for test vector directories), and
  `find <your dirs> -type f -size +300k` must print nothing.
- At most 2 compile jobs at a time (`-j2`), `nice -n 10`. No downloads, no package installs, no `sudo`.

## 3. The phone and private material
- **No phone access**: no adb, fastboot, `phone.py`, `hwc.py run/step`, `txn.sh`, no USB. The main session flashes and runs phone tests;
  you prepare scripts and say exactly what to run and what output means pass.
- The vendor files under `gpu/build/cam-vendor-20261109/vfs` are the user's own phone's proprietary binaries. Tests may **read** them at
  run time from that path. Never copy them, never paste disassembly or decompiled code longer than ~20 lines into the tree, never
  publish anything. Facts (addresses, symbol names, import lists, struct layouts, register offsets) are fine.
- No private tegu-derived source, no Apple SDK headers, no dumps of Apple binaries in the tree. Symbol names and addresses are fine.
- Licences: your new code is MIT (`/* SPDX-License-Identifier: MIT */` first line). Text taken from the Android kernel tree
  (`nord2-current-kernel`, GPL-2.0) — struct definitions, register names — goes only into files that start with
  `/* SPDX-License-Identifier: GPL-2.0 */` and whose name begins with `mtk_`; keep those minimal. Do not open or reproduce QEMU,
  Unicorn, dynarmic, FFmpeg, x264 or libjpeg source. Installed tools (ffmpeg, djpeg, cjpeg, llvm-mc, llvm-objdump, clang) are
  black-box oracles. The instruction semantics come from the Arm Architecture Reference Manual (ARMv7-A/R and ARMv8-A AArch32)
  as you know it; when unsure of an encoding, assemble it with `llvm-mc --show-encoding` and disassemble with `llvm-objdump`.

## 4. Code
- C11, clang, portable: x86-64 Linux and arm64 iOS, little-endian. No compiler-specific assembly, no JIT, no `long double`,
  no host `float` behaviour assumptions beyond IEEE 754 (use `fesetround`/softfloat-style code where FPSCR modes matter).
- Host test flags: `-std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=all -I gpu/hwcodec/include`,
  and once more with `-O2` without sanitizers. Threaded code additionally with `-fsanitize=thread`.
- Kernel code (`kern/**`) follows `gpu/hwcodec/kernel/b/bsd/dev/nord2_hwcodec.inc` (the verified model): every wait bounded, nothing
  runs at boot, never touch a block's registers unless its domain is on and unprotected, never write registers of other users,
  log lines `NORD2 <TAG>:`. It must build as a host test against a register model with `-DN2_HOST` exactly like `gpu/hwcodec/test/t_hwcodec.c`.
- No stubs presented as done. An unimplemented case returns the documented error (`N2A32_UNDEF`, `-ENOSYS`, …) and is **listed**
  in your STATUS. Never `abort()`, never print from library code (use the trace hooks / return codes).

## 5. Tests
- Each package has `<dir>/run_tests.sh` (bash, `set -euo pipefail`, no arguments, builds into `/tmp/n2hw-<id>/`). Last line on success:
  `N2TEST <id> PASS <passed>/<total>`; on failure `N2TEST <id> FAIL <what>` and exit 1. It must finish in under 2 minutes.
- Expected values must not come from your own implementation: use literals worked out from the specification, the oracle tools,
  or the differential harness of T01 (the same C compiled for ARM and run under the interpreter vs compiled natively).
- State exactly what was tested. "Host-tested", "compiled for iOS" and "run on the phone" are three different statements; you can
  only claim the first two.

## 6. Board (the only coordination channel)
    python3 gpu/hwcodec/board/post.py post --from <your sender id> --to <hw-id|all|hw-lead> --subject '[hw] ...' --body '...'
    python3 gpu/hwcodec/board/post.py read --limit 40
- Read the board before you start, every ~20 tool calls, and before finishing. Answer questions addressed to you first.
- Post when: you start (one line), you find a fact others need (also add it to FACTS.md through its owner `hw-d01`), you are blocked
  (say by whom and what you need, then continue with a local stand-in), you finish.
- Facts go to `hw-d01` as: statement, evidence (path:line / symbol / command), `[V]` verified or `[I]` inferred. Only `hw-d01` edits
  `FACTS.md`.
- Integrators (`hw-iv`, `hw-ik`, `hw-ip`) own cross-package glue; give them what they ask for quickly. Reviewers (`hw-r1`, `hw-r2`)
  post findings; fix every finding or answer why not, on the board, before you post DONE again.

## 7. Finishing
- Write `<dir>/STATUS.md`: what is done, what is not, the exact commands and their last lines, known risks, open questions.
  Honest: an unfinished item listed is fine, a hidden one is not.
- Post to `all`: subject `[hw] DONE <id>`, body = the last line of your `run_tests.sh`, the list of not-done items, the files changed.
- After DONE, stay available: re-read the board, fix review findings, then post `[hw] DONE <id> rev N`.
