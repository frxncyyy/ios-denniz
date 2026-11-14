# T02 fake hardware (`n2vc_hw_open_fake`)

`n2vc_hw_fake.c` implements the frozen `struct n2vc_hw` of
`include/n2vc_hw.h` for host tests. Integrators include `n2vc_hw.h` only;
the extra controls are in `n2vc_hw_fake.h` (this directory).

```c
struct n2vc_hw hw;
n2vc_hw_open_fake(&hw, "/tmp/record.txt");   /* record_path NULL = no recording */
n2vc_hw_fake_script(&hw, "scripts/basic.txt");
hw.power(hw.ctx, RWVC_BLK_VENC, 1);
n2vc_hw_wr(&hw, RWVC_WIN_VENC, 0x10, 1);
printf("violations %u\n", n2vc_hw_fake_violations(&hw));
hw.close(hw.ctx);
```

`n2vc_hw_open_fake()` also loads `$N2VC_FAKE_SCRIPT` when it is set, so a
test can configure the fake without an extra call.

## Power model

Four blocks (`RWVC_BLK_*`) with user reference counts. `VENC_C1` takes a
hidden reference on `VEN` and `VDEC_CORE` on `VDEC_SOC`, exactly as
`RWVC_POWER` is documented in `kern/VCODEC_HW.md` section 7. A register
operation on a window whose block is not powered returns `-EACCES` and
increments the violation count. The window -> block table is K02's:
VENC, VENC_C1; VDEC_CORE for VDEC_SYS/BASE/VLD/MC/MV/MISC/RACING; VDEC_SOC for
LAT_MISC/LAT_VLD/SOC_GCON.

## Script grammar

One rule per line, `#` starts a comment, numbers are decimal or `0x...`,
window names are `VENC`, `VENC_C1`, `VDEC_SYS`, `VDEC_BASE`, `VDEC_VLD`,
`VDEC_MC`, `VDEC_MV`, `VDEC_MISC`, `VDEC_LAT_MISC`, `VDEC_LAT_VLD`,
`VDEC_RACING`, `VDEC_SOC_GCON` (or `0..11`):

```text
after write VENC 0x0010 0x00000001&0x0000000f -> set VENC 0x0014 0x00000002
after write VENC 0x0030 0x00000001 -> set VENC 0x0034 0x00000001
irq 0 fires 0 ms after write VENC 0x0030 0x00000001
irq 1 fires 20 ms after write VENC 0x0030
read VENC 0x0020 returns 0x12345678
read VENC 0x0024 returns 0xdeadbeef once
irq 2 status 0x00000010
set VENC 0x0008 0xdeadbeef
```

- `VALUE&MASK` triggers when `(written & mask) == (value & mask)`; for a
  masked write the trigger value is `value & mask`. Without `VALUE` any write
  to the offset triggers.
- A `read` rule takes precedence over the register array. `once` makes it a
  single read; a persistent rule keeps returning the value.
- Set rules chain; a chain longer than 32 returns `-ELOOP` and counts as a
  violation. A set target whose block is not powered returns `-EACCES`.
- `set WIN OFF VALUE` is a load-time initial register value, not a register
  access (the block need not be powered). It is written when the script is
  loaded and recorded as `SCRIPT default ...`.
- `irq N fires ...` schedules a pending interrupt; `wait_irq` blocks (1 ms
  steps) until the delay elapses or its timeout expires. `irq_clear` drops
  pending and scheduled events for that line.
- A malformed script is rejected with `-EINVAL`; the previous rule set is
  kept. `n2vc_hw_fake_last_error()` names the line.

## Recorder

Every operation writes one flushed line:

```text
POWER VENC on -> 0
W VENC 0x0010 0x00000001
W VENC 0x0010 0x00000003 mask 0x0000000f
R VENC 0x0010 0x00000001
R VENC 0x0020 0x12345678 script
POLL VENC 0x0010 0x00000001 mask 0x00000001 -> ok reads=1
DELAY 100 us
IRQ wait 0 timeout_ms=100 -> 0 status=0x00000001
IRQ clear 0
SCRIPT set VENC 0x0014 = 0x00000002
SCRIPT irq 0 in 5 ms
DMA alloc handle=1 pa=0xda800000 bytes=4096 flags=0
DMA free handle=1
DMA rw handle=1 off=0x0000 bytes=16 W
```

## DMA

First-fit 4 KiB pages in `RW_CLAIM_VCODEC_PA..+RW_CLAIM_VCODEC_BYTES`, host
`calloc` backing (zero-filled), stable handles, `dma_rw` is a plain copy.
`n2vc_hw_fake_dma_host()` returns the host mirror for tests.

## Limits

- `regs()` has no `poll_timeout_us` field in the frozen callback, so POLL uses
  `n2vc_hw_fake_set_poll_timeout()` (default 100 ms; 0 = one read).
- `wait_irq()` accepts 0..5000 ms, like the kernel.
- `n2vc_hw_fake_script()` is not meant to be called concurrently with a
  running operation; load scripts before the guest starts.
- `n2vc_hw_fake_stats()`/`violations()` are plain reads, intended after the
  operations (the host tests call them after `pthread_join`).
- Each fake instance owns its own DMA pool and its own register arrays; two
  instances can return the same physical address. Host tests use one.

## Test

```sh
bash gpu/hwcodec/tests/hw/run_tests.sh
# N2TEST T02-hw PASS 6170/6170
```

Builds only under `/tmp/n2hw-T02/hw`; ASan+UBSan, `-O2`, and TSan.
