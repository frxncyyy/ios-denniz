# Parameterized attachment-clear RT1 state

`attachment_clear_rt1_state.{c,h}` is a host-qualified sibling of the exact
16x16 `attachment_clear_r8_state` builder. It keeps that builder and its phone
oracle unchanged. The sibling accepts BGRA8 target dimensions from 16 through
4096, a 64-byte-aligned linear row stride, and at most 16 MiB of active target
bytes. It still represents only the exact TAC1 shader/resource ABI: logical RT0
disabled, logical RT1 active, masks 0/15, one rect16 buffer, one matrix320
buffer, and the existing 59-word draw stream. The `clear_rgba8` state-builder
argument is copied into all four RT1 clear words; request-layer handling for a
DONTCARE action remains outside this encoder.

The GPU aliases follow the canonical full-size allocator rather than the R8
aperture tail:

1. 112 scratch pages at `0x06000000`;
2. `ceil(row_stride * height / 4096)` target pages;
3. one contiguous resource page; and
4. the fixed 512-page (2 MiB) initial tiler heap.

This ordering is the contract in `fullscreen_job_contract.h` and
`TeguGPUFullscreenAllocation.inc`. The caller must use the returned map,
zero the whole heap, retain every backing through completion, and apply the
same full-size mapping checks. The state builder does not allocate, map,
flush, submit, or wait.

`target_length` is the backing allocation length, not the descriptor's active
surface stride. It must be 4 KiB aligned and cover the rounded final target
page. The current kernel caller supplies `IOMemoryDescriptor::getLength()`;
`TeguGPUBuffer::allocate` and the admitted IOSurface metadata provide an
allocation-rounded length. The active `row_stride * height` remains in the RT1
surface-stride field.

## Field provenance

Every field that differs from the R8 state is limited to this table. The caller supplies the packed BGRA clear word; the builder duplicates it into all four clear channels, and the R8 regression passes `0x11223344` to preserve byte parity.

| Bytes / command | Meaning | Source |
|---|---|---|
| FBD words 8 and 10 | `(width - 1) | ((height - 1) << 16)` for dimensions and maximum bounds | Mesa generated `genxml/v10_pack.h`, `MALI_FRAMEBUFFER_PARAMETERS_pack` |
| viewport words at `0xa00/0xa04/0xa10/0xa14` | exact binary32 `width/2`, `height/2` | existing `compositor_render_state_v6.c` full-size builder |
| RT1 words 8–11 | target alias, row stride, surface stride | Mesa generated `MALI_RGB_RENDER_TARGET_pack` |
| RT1 words 12–15 | four copies of requested `clear_rgba8` | Mesa generated `MALI_RT_CLEAR` fields in `MALI_RGB_RENDER_TARGET_pack` |
| tiler-context words 2–3 | full-size hierarchy mask `0x82`, framebuffer extent | Mesa generated `MALI_TILER_CONTEXT_pack`; qualified full-size v7 builder |
| tiler-heap words 1–7 | 2 MiB size, base, base+64 bottom, base+size top | Mesa generated `MALI_TILER_HEAP_pack`; qualified full-size v7 builder |
| resource entry 1 and buffer descriptors 0/1 | relocated table, rect16 and matrix320 aliases | Mesa generated `MALI_RESOURCE_pack` / `MALI_BUFFER_pack`; exact TAC1 resource ABI |
| CS R0 | relocated table1 handle (`resource_va | 4`) | existing TAC1 R8 stream and v7 resource-table builder |
| both CS R43 moves | packed framebuffer extent | existing full-size render-state builder |

The host test decodes the FBD, both render targets, tiler context, tiler heap,
resource entry, and both buffer descriptors with the pinned Mesa v10 packers.
It also checks exact command words and compares a 16x16 build byte-for-byte
against the R8 phone oracle outside only the new full-size heap fields. A
776x24/3136-byte-stride case proves the parameterization, but does not claim
that Backboard's attachment-clear render target has those dimensions. A
1080x2424/4352-byte-stride Pixel-class case verifies the two-L3-size address
layout and rounded final-page contract. The packed display stride 4320 is
outside this builder's 64-byte-aligned target class.

Run both builders' checks with:

```sh
python3 tools/tegu/test_attachment_clear_r8.py
python3 tools/tegu/test_attachment_clear_rt1.py
```

## Remaining qualification

This builder has not run on the phone. Before integration it still needs a
kernel-side owner that feeds its returned target/resource/heap aliases into the
existing full-size mapper, enforces the TAC1 package identity, and submits the
state with the qualified queue lifecycle. A bounded phone run must first prove
an independently known full-size BGRA output and guards, then the real
Backboard pass after its dimensions and resource identities are observed.
