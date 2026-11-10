# R2 integration notes (for the lead)

`avc2/n2avc_deblock.c` is complete and host-tested (see `STATUS.json`, `../../dsh/workers/R2/STATUS.md`). It is inert until
the three edits below are in `avc2/n2avc_core.c`; the suite currently applies them to a build-directory copy
(`patch_core.py`). The P_Skip fix in item 4 belongs to WP04's `avc/n2avc_inter.c` and is also applied to a build copy only.

## 1. The function

```c
void n2avc_deblock_picture(n2avc_frame *rec, const n2avc_mbinfo *mb, int mb_w, int mb_h);
```

- `rec` is the reconstruction of the picture being coded, before `extend_plane`; the filter never reads outside the picture.
- `mb` is `e->mb`, raster order `[mby * mb_w + mbx]`; `mb_w`/`mb_h` are the picture size in macroblocks.
- Preconditions: every slice has `disable_deblocking_filter_idc == 0` and both filter offsets 0 (what the slice-header edit
  below writes); macroblocks are I16/I_PCM/P16/P_Skip; motion vectors are quarter luma samples.
- No allocation, no libc, no globals, no imports; it is safe to call from the one thread that has joined the slice threads.

## 2. `n2avc_open`: accept the flag

Delete:

```c
	if (c.flags & N2AVC_F_DEBLOCK)
		return N2AVC_E_ARGUMENT;
```

## 3. Slice header (in `slice_run`, where `disable_deblocking_filter_idc` is written)

Replace:

```c
	n2bw_ue(&s->bw, 1);                                     /* disable_deblocking_filter_idc */
```

with:

```c
	if (e->cfg.flags & N2AVC_F_DEBLOCK) {
		n2bw_ue(&s->bw, 0);                             /* filter everything, including slice edges */
		n2bw_se(&s->bw, 0);                             /* slice_alpha_c0_offset_div2 */
		n2bw_se(&s->bw, 0);                             /* slice_beta_offset_div2 */
	} else {
		n2bw_ue(&s->bw, 1);
	}
```

When idc is 0 the two `se(0)` fields are mandatory; omitting them shifts the rest of the slice header and ffmpeg reports
syntax errors. The bit writer's alignment means the packet size is unchanged (ue(1) and idc 0 + two se(0) are all 3 bits), but
the slice data is unaffected anyway.

## 4. The call (in `n2avc_encode`, after the slice joins and the overflow retry, before `extend_plane`)

```c
	if (e->cfg.flags & N2AVC_F_DEBLOCK)
		n2avc_deblock_picture(rec, e->mb, e->mb_w, e->mb_h);
	extend_plane(&rec->y, W, H, N2AVC_PAD);
```

plus a prototype (`extern` in `avc2/n2avc_core.c` or in the frozen header, the lead's choice). The order matters: the
deblocked picture is what the next picture's motion compensation must see, so the call must be before the swap and before
`n2avc_reconstruction()` returns; `extend_plane` must run after it (it only replicates the border).

## 5. P_Skip, clause 8.4.1.1 (WP04's `avc/n2avc_inter.c`) — still wrong in the tree

The current `n2avc_inter_skip_mv` forces (0,0) when A or B is intra. Clause 8.4.1.1 (11/2007, p.147) has no intra condition:
only unavailable A/B, or an inter A/B with refIdxL0 == 0 and mv (0,0). Replace the body with:

```c
	const n2avc_mbinfo *a, *b;

	if (!n2avc_avail(s, mbx - 1, mby) || !n2avc_avail(s, mbx, mby - 1)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	a = &s->mb[mby * s->mb_w + mbx - 1];
	b = &s->mb[(mby - 1) * s->mb_w + mbx];
	if ((a->ref == 0 && a->mvx == 0 && a->mvy == 0) ||
	    (b->ref == 0 && b->mvx == 0 && b->mvy == 0)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	n2avc_inter_pmv(s, mbx, mby, mvx, mvy);
```

Evidence is on the board (R2 message of 04:42): ffmpeg's reconstructed P_Skip MBs are copies of the reference at the median
vector, not at (0,0), when A is intra. With this fix the 178x146 squares QP26 threads4 P case becomes bit-exact with
deblocking. `patch_inter.py` applies it to a build copy.

## 6. Plug-in build

`build_ios.sh` must compile `avc2/n2avc_deblock.c` together with the avc2 core/inter/me/rc set (it currently globs `avc/*.c`).
The file compiles for `arm64-apple-ios16.0` with the PLAN 4.3 flags and imports nothing.

## 7. Test command

```
bash gpu/video/avc/test/deblock/run_tests.sh
```

Last line: `N2TEST R2 PASS 518/518`. Once items 1–4 are in the real core (and item 5 in the real inter file), the same
command uses the real files: `patch_core.py` and `patch_inter.py` detect the fixes and copy the sources unchanged.
