# DEC03 -> T02: the fake register hook (exact integration)

`n2vld_hook` is ready in `n2vld.c`; the missing piece is a way for the fake to call it. The
signature below is the one `t_vld.c`'s private bridge already uses. It is deliberately small:

```c
typedef int (*n2vc_hw_fake_reg_hook)(void *ctx, unsigned win, uint32_t off,
                                     int is_write, uint32_t *value);
int n2vc_hw_fake_set_reg_hook(struct n2vc_hw *hw, n2vc_hw_fake_reg_hook fn, void *ctx);
```

Return value: `1` = handled (`*value` is the read result for a read, or the value the hook stored
for a write); `0` = use the fake's normal array/rules; `<0` = error.

Call it only **after** `check_reg()` and the `block_on()` power check. The fake keeps ownership of
power, IRQs, DMA and every register the model does not claim.

## The four call sites in `fake_regs()`

1. **READ** (before `find_read_rule`/`read_word`):
   ```c
   if (f->hook) {
       uint32_t hv = 0;
       int hr = f->hook(f->hook_ctx, o->window, o->offset, 0, &hv);
       if (hr < 0) { rc = hr; break; }
       if (hr > 0) { v = hv; /* record as usual */ break; }
   }
   ```
2. **WRITE**: same with `is_write = 1`; if `hr > 0`, skip `write_word`/`apply_rules`.
3. **WRITE_MASK**: the hook has no mask parameter, so merge through its own shadow:
   ```c
   old = read_word(f, o->window, o->offset);
   if (f->hook) { uint32_t hv = old;
                  hr = f->hook(..., 0, &hv); if (hr > 0) old = hv; }
   v = (old & ~o->mask) | (o->value & o->mask);
   if (f->hook) { uint32_t hv = v;
                  hr = f->hook(..., 1, &hv); if (hr > 0) v = hv; }
   write_word(f, o->window, o->offset, v);
   ```
   This matters for AVC `0x2c8`, VLD `0xec`/`0x140` and the larb word.
4. **POLL**: inside the per-read loop, ask the hook for a read before the fake's array:
   ```c
   uint32_t hv = 0;
   if (f->hook && f->hook(..., 0, &hv) > 0) v = hv;
   else { ... find_read_rule/read_word ... }
   ```
   Without this, polls of VLD `0xe8` / AVC `0x2d8` never see `fetch_ready` / search state.

## Windows and power

- The model's default table: win 4 VLD (core), win 7 core MISC + HEVC, win 8 LAT MISC, win 9
  LAT VLD, win 12 `VDEC_ALL` (base `0x16000000`, size `0x30000`).
- `RWVC_WIN_VDEC_ALL` (12) is a proposed rev-3 header value and is needed for `LAT_AVC_VLD`
  `0x12000` and the vendor's other VDEC pages. DEC07/DEC04 own that text.
- The fake must power the block the window maps to before calling the hook; the model does not
  check power. `VDEC_CORE` for core pages (`>= 0x20000`), `VDEC_SOC` for LAT/MISC (`< 0x20000`).

## Testing after the hook

`bash gpu/hwcodec/tests/hw/vdec/run_tests.sh` still passes; then add a fake-side test that
programs a vfifo, writes `VLD 0x23 = 0x100000`, reads `VLD 0x3a` bit 0 and reads `AVC 0x0`/`AVC 0x8f`.
The expected values are in `REGISTERS.md` §8. No phone, no publishing.
