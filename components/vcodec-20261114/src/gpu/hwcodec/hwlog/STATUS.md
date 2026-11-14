# X01 status — hardware op log, log2script, diff_logs

Last update: 11 November 2026, rev 1. **Host-only. No phone access, no
flashing, no publish, no daemon edit.** Every file is under
`gpu/hwcodec/hwlog/`; the only other write is this worker status file.

## Implemented

| File | What |
|---|---|
| `n2vc_hw_log.h` | frozen-style public API: `int n2vc_hw_wrap_log(struct n2vc_hw *out, const struct n2vc_hw *inner, const char *path)`; line syntax and guarantees |
| `n2vc_hw_log.c` | forwarding decorator for all eight callbacks; T02 recorder syntax with a decimal µs timestamp and an explicit result; CRC-32 (never data) for `dma_rw`; mutex; stdio buffering; `fflush`+`fsync`+`fclose`; `N2HW_HWLOG_MAX_MB` (default 16) with a final `HWLOG truncated` line; test-only clock seam |
| `log2script.py` | phone/PC log → T02 script (`read ... once`, `irq N status`, `irq N fires ... after write`) and per-register summary (`writes/reads/polls/first/last/masks/read_values`) |
| `diff_logs.py` | first divergent register write between two logs; timestamps ignored; DMA addresses inside each log's pool region normalised by its inferred base; `--all` for every op; exit 0/1/2 |
| `README.md` | exact vcodecd hook, phone run steps, which file to copy back, analysis commands, limits, privacy note |
| `tests/t_hwlog.c` | literal op sequence, observations file, error forwarding, NULL arguments, truncation, two-thread run |
| `tests/expected.log` | byte-exact expected log for the literal sequence |
| `tests/t_check.py` | 35 Python checks for script/summary content, raw T02 logs, malformed input, DMA-base normalisation, masked writes, lengths |
| `run_tests.sh` | ASan+UBSan, `-O2`, TSan, cap-0 builds, iOS freestanding compile |

## Tested

Command:

```sh
bash gpu/hwcodec/hwlog/run_tests.sh
```

Last run (13-33 s depending on host load, 11 November 2026):

```text
== asan: literal sequence and observations
X01 C PASS 36/36
== asan: replay through the generated script
X01 C PASS 36/36
X01 SH CK ok: asan reads round trip
X01 SH CK ok: asan full log round trip
== asan: error forwarding
X01 C PASS 41/41
== asan: threads
X01 C PASS 14/14
X01 SH CK ok: asan literal expected log
== o2: literal sequence and observations
X01 C PASS 36/36
== o2: replay through the generated script
X01 C PASS 36/36
X01 SH CK ok: o2 reads round trip
X01 SH CK ok: o2 full log round trip
== o2: error forwarding
X01 C PASS 41/41
== o2: threads
X01 C PASS 14/14
X01 SH CK ok: o2 literal expected log
== tsan: literal sequence
X01 C PASS 36/36
X01 SH CK ok: tsan literal expected log
== tsan: threads
X01 C PASS 14/14
== cap0-asan: truncation
X01 C PASS 9/9
== cap0-o2: truncation
X01 C PASS 9/9
X01 PY PASS 35/35
== iOS compile with build_ios.sh's freestanding flags
X01 SH CK ok: n2vc_hw_log.c compiles for arm64-apple-ios16.0
N2TEST X01 PASS 439/439
```

What those checks actually establish:

- The literal sequence produces `tests/expected.log` **byte for byte** under a
  deterministic test clock; the timestamps are not wildcarded.
- `log2script.py` output loaded into T02's fake reproduces **both** the read /
  IRQ observation file and the full replayed log (same 29 lines, same values).
- A mutated write makes `diff_logs.py` print exactly
  `A line 12: ... W VENC 0x0040 0x0000000a ...` and
  `B line 12: ... W VENC 0x0040 0x0000000b ...`.
- Logs whose DMA pools start at `0xda800000` and `0xdb000000` compare
  identical in writes; `--all` still reports the genuinely different read.
- Masked writes ignore bits outside the mask and compare bits inside it.
- `N2HW_HWLOG_MAX_MB=0` produces exactly one line,
  `1000000 HWLOG truncated limit_bytes=0 bytes=0 dropped=5`.
- Two threads doing 50 iterations each (DMA alloc/rw/free plus a 3-op batch)
  produce 702 well-formed, timestamp-monotonic lines; TSan reports nothing.
- The source compiles with build_ios.sh's exact flags: `-target
  arm64-apple-ios16.0 -nostdinc -isystem gpu/video/sdk/sysinc -Werror`.

Not claimed: anything on the phone, anything through P03's real
`/dev/redwood-vcodec`, or a product vcodecd link.

## Not done / limitations

- **No phone run.** The hook is posted to hw-ip; until it lands, vcodecd
  ignores `N2HW_HWLOG`. The README names the exact 3-line insertion point and
  the build_ios lines.
- **Not linked into vcodecd.** `_fileno` is the only libSystem symbol the new
  object imports that `daemon/sdk_symbols.txt` lacks; hw-ip was asked to add
  it. Everything else in the object is already listed.
- **No real vendor log has been converted.** `facts/VENC_PROTOCOL.md` is
  read-only for this package (opus-venc/hw-d01); the summary command is ready,
  but no rows have been added.
- `POLL` has no `reads=N`: the frozen callback returns only `done` and the
  result. `ok`/`timeout` is exact; the line has the trigger value, not the
  observed value, so `diff_logs --all` compares only trigger and outcome; a
  successful replay returns the trigger value (outside-mask bits zero).
- `DMA alloc bytes=` is the requested size, not T02's page-rounded size;
  `diff_logs --all` rounds both.
- An IRQ delay is derived from the last write before a successful wait. There
  is one rule per `(irq, trigger write)` with the first observed delay; later
  occurrences with a different delay are not represented separately.
- A timed-out `POLL` has no observable read value; it is not turned into an
  invented read override. If the replayed register already matches,
  `log2script.py` warns that the fake will succeed.
- One `irq N status` rule per IRQ holds the last observed status; a status
  that changes between waits is not represented.
- The VENC/VENC_C1 window is 0x3000 in K02 rev 3 but 0x2000 in T02's fake.
  `log2script.py` accepts 0x3000 (so a real log is not filtered); a replay
  rule in 0x2000..0x2ffc is rejected by the current fake until T02 updates
  `WINS`. Posted to hw-t02; the frozen `redwood_vcodec.h` comment (0x2000)
  was posted to hw-lead. No host test uses the upper half.
- The truncation notice is written by `close()`; a SIGKILL can lose the last
  stdio buffer and the notice. A clean SIGTERM (vcodecd's signal path) closes
  and fsyncs.
- The test clock seam (`n2vc_hw_log_test_set_clock`) is a global set before
  wrapping; it is not synchronised with running wrappers. Production code
  never calls it (uses `CLOCK_MONOTONIC`).
- The log is created with `fopen` defaults (umask, usually 0644); the README
  asks for a private directory (`0700`). No `chmod` symbol is added. The log has no pixel data, only register values, DMA
  addresses, results and CRC-32, but it is still phone-private.

## Open questions / coordination

- hw-ip: accept the hook placement and add `_fileno` + the build_ios object
  line? Posted.
- hw-t02: is the extra `-> result` suffix and `crc32=` field acceptable to any
  consumer of the recorder format? Posted; no reply yet, no change requested.
- opus-venc: once a real phone log exists, the summary rows are the input for
  `facts/VENC_PROTOCOL.md`; the exact row syntax is in the README.

## Files

`gpu/hwcodec/hwlog/{n2vc_hw_log.c,n2vc_hw_log.h,log2script.py,diff_logs.py,
README.md,STATUS.md,run_tests.sh,tests/{t_hwlog.c,t_check.py,expected.log,
scripts/ops.txt}}`.
