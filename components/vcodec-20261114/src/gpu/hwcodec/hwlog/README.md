# X01 — record every `n2vc_hw` call, replay it as a fake-hardware script

`hwlog/` is a **logging decorator** around the frozen `struct n2vc_hw`
(`include/n2vc_hw.h`) plus two host tools. It does not implement hardware and
does not replace any back end: it forwards every call unchanged and appends
one text line per operation.

| File | What |
|---|---|
| `n2vc_hw_log.h` | public interface, line syntax, guarantees |
| `n2vc_hw_log.c` | `n2vc_hw_wrap_log()`, CRC-32, cap/truncation, mutex |
| `log2script.py` | phone log → T02 fake-hardware script + per-register summary |
| `diff_logs.py` | first divergent write between a PC fake log and a phone log |
| `tests/` | literal op sequence, expected log, C and Python checks |
| `run_tests.sh` | the whole host suite (ASan/UBSan, -O2, TSan, iOS compile) |

## 1. Wiring it into vcodecd (owner: hw-ip)

`n2vc_hw_log.c` compiles for `arm64-apple-ios16.0` with `build_ios.sh`'s exact
freestanding flags (`-nostdinc`, `gpu/video/sdk/sysinc`, `-Werror`). It imports
only libSystem calls already in `daemon/sdk_symbols.txt` except `_fileno`
(`fsync(fileno(fp))` on close), which hw-ip adds.

The exact hook, after the `--dma-dir` wrapper and before `n2vhost_start`, so
that the log sees the final back end:

```c
struct n2vc_hw hwlog; const char *hwlog_path = getenv("N2HW_HWLOG");
if (hwlog_path != NULL && *hwlog_path != '\0' && n2vc_hw_wrap_log(&hwlog, &g.hw, hwlog_path) == 0)
	g.hw = hwlog;
```

plus `#include "n2vc_hw_log.h"`, `-I "$HERE/hwlog"`, and
`hwlog/n2vc_hw_log.c` in `DAEMON_OBJS`. The 3-line hook and the build lines
were posted to `hw-ip` on the board.

If `N2HW_HWLOG` is unset, empty, or the wrap fails, `g.hw` is untouched and
vcodecd behaves exactly as before.

## 2. What the main session runs on the phone

Workers never touch the phone. The main session does this on an explicitly
authorized build:

1. Put a vcodecd with the hook on the phone and create a private directory
   for the log (`/var/mobile/n2hw`, mode 0700) with the main session's install
   transaction or file tool. **There is no `/bin/sh` on the phone**, so no
   shell snippet below is meant to run there.
2. Add `N2HW_HWLOG=/var/mobile/n2hw/hwlog.txt` to vcodecd's launchd plist
   `EnvironmentVariables` (the plist is owned by hw-ip/opus-ios) and restart
   the daemon. A client-side environment variable is irrelevant. The
   `N2HW_HWLOG=... vcodecd` shell form is for a host test only.
3. Run one known encode session (the `n2vcd_cli enc ...` command or the
   VideoToolbox test). Every `n2vc_hw` call made by the hosted vendor code is
   recorded.
4. **Stop vcodecd cleanly** through launchd (`launchctl stop <label>`, or
   kickstart it after the capture) or the main session's process control, and
   wait for exit before copying. The decorator buffers with stdio and does
   `fflush` + `fsync` + `fclose` in `close()`; the daemon's SIGTERM path calls
   it. A SIGKILL can lose up to the last stdio buffer and the final truncation
   line.
5. **Copy back exactly one file** with the main session's file transfer: the
   path given to `N2HW_HWLOG` (`/var/mobile/n2hw/hwlog.txt` in the example).
   Nothing else is needed.

The log is overwritten at each vcodecd start (`fopen(path, "w")`). The file is
created with the process umask (usually 0644); put it in a 0700 directory
rather than relying on the file mode. It contains register offsets/values,
DMA physical addresses, results and CRC-32 of DMA buffers — **never pixel
data** — but it is still from the user's phone: keep it out of git and do not
publish it.

## 3. Analysing the log on the PC

```sh
# T02 fake-hardware script + per-register summary
python3 gpu/hwcodec/hwlog/log2script.py phone.log \
        --script replay.txt --summary registers.txt

# PC fake run vs phone run: first divergent write (DMA pool base ignored)
python3 gpu/hwcodec/hwlog/diff_logs.py pc.log phone.log

# also compare reads, polls, IRQ and DMA lines
python3 gpu/hwcodec/hwlog/diff_logs.py --all pc.log phone.log
```

`pc.log` may be a raw T02 recorder file (no timestamps, no `-> result`
suffix) or an X01 phone-format log; both parsers share the same code. The
default comparison is the sequence of register writes, because that is the
protocol and because read values legitimately differ between a model and the
block. `--all` includes reads/polls/IRQ/DMA for a full replay check.

`log2script.py` writes T02 grammar only. A raw T02 log's `SCRIPT set` /
`SCRIPT irq` lines are treated as **effects**, not copied: the generated script
reproduces their observable result with read overrides and explicit `irq`
rules, so a rule chain is not duplicated on replay.

```text
read VENC 0x0024 returns 0xdeadbeef once
irq 0 status 0x00000001
irq 0 fires 4 ms after write VENC 0x0030 0x00000001
```

Load the result with `n2vc_hw_fake_script()` and run the same operation
sequence; the reads and interrupt order are reproduced. The summary rows are
meant for `facts/VENC_PROTOCOL.md` (owner opus-venc / hw-d01):

```text
VENC 0x0010 writes=2 reads=1 polls=0 first=0x00000001 last=0x00000003 masks=0xffffffff,0x0000000f read_values=0x00000003
```

## 4. Log format

One line per `n2vc_hw` operation, one line per register op inside a batch,
T02 recorder syntax with a decimal microsecond timestamp in front and an
explicit result after:

```text
123456 POWER VENC on -> 0
123457 W VENC 0x0010 0x00000001 -> 0
123458 W VENC 0x0010 0x00000003 mask 0x0000000f -> 0
123459 R VENC 0x0010 0x00000001 -> 0
123460 POLL VENC 0x0010 0x00000001 mask 0x00000001 -> ok
123461 DELAY 100 us -> 0
123462 IRQ wait 0 timeout_ms=100 -> 0 status=0x00000001
123463 IRQ clear 0 -> 0
123464 DMA alloc handle=1 pa=0xda800000 bytes=4096 flags=0x0 -> 0
123465 DMA free handle=1 -> 0
123466 DMA rw handle=1 off=0x0000 bytes=16 W crc32=0xecbb4b55 -> 0
```

CRC is IEEE 802.3 CRC-32 (reflected polynomial `0xedb88320`, init/xorout
`0xffffffff`); `123456789` gives `0xcbf43926`. The data itself is never
logged. The CRC covers the whole DMA buffer, so the log costs CPU on the
phone; enable it for a capture, not permanently.

Known limits, stated so nobody reads more into a log than it has:

- `POLL` has no `reads=N`: the frozen callback returns only `done` and the
  result, not the number of reads. `ok`/`timeout` is exact. The line carries
  the trigger value, not the observed value, so `diff_logs --all` compares the
  trigger and the outcome, not the bits outside the mask. On replay a
  successful poll returns the trigger value (outside-mask bits zero); that is
  enough for the poll but is not a claim about the real read value.
- `DMA alloc bytes=` is the **requested** size; T02's recorder prints its
  page-rounded size. `diff_logs --all` rounds both before comparing.
- A write/read that the back end rejected is logged with the error; a failed
  `R` has no value and prints `?`.
- `status=none` means the caller passed a NULL status pointer; the status was
  still forwarded but is not observable.
- A failed batch logs the ops that completed and the op at index `done` with
  the batch error, then stops. With a NULL `out_done` it logs only the batch
  result, because the failing index is not observable.
- Interrupt delays in `log2script.py` come from the timestamp of the last
  write before the successful wait. There is one rule per
  `(irq, trigger write)` with the first observed delay; a later occurrence of
  the same write with a much longer delay is not represented separately.
- A timed-out `POLL` does not invent a read value. If the replayed register
  array already matches the poll value, `log2script.py` warns; the fake will
  succeed where the phone timed out.
- An IRQ's status line is the last status observed for that line (T02's
  grammar has one load-time `irq N status` rule); a status that changes
  between waits is not represented.
- `log2script.py` accepts the VENC/VENC_C1 window as 0x3000 (K02 rev 3:
  vpud maps 0x3000, JPGENC starts at 0x17030000). T02's fake still models
  0x2000, so a real log with an offset in 0x2000..0x2ffc produces a read rule
  the current fake rejects; hw-t02 was notified. No host test uses the upper
  half.
- Size: at most `N2HW_HWLOG_MAX_MB` (default 16, compile-time macro) of
  operation lines; then the last line is
  `HWLOG truncated limit_bytes=... bytes=... dropped=...`.

## 5. Host tests

```sh
bash gpu/hwcodec/hwlog/run_tests.sh
# last line: N2TEST X01 PASS 439/439
```

Builds only under `/tmp/n2hw-X01`: ASan+UBSan, `-O2`, TSan, and a
`-DN2HW_HWLOG_MAX_MB=0` build for truncation. The literal expected log is
byte-compared using the test clock; the generated script is fed back into T02's
fake and both the observations and the full log must match; `diff_logs` must
report the exact mutated line; the source is also compiled with
`build_ios.sh`'s freestanding arm64-ios16 flags.

Host-tested only. No phone run and no iOS link in a product vcodecd were done
by this package; the phone steps above are for the main session.
