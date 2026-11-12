# DEC07 patches: status

| Patch | Status |
|---|---|
| `redwood_vcodec-rev3.patch` | **pending**: posted to hw-lead. `include/redwood_vcodec.h` rev 3: `RWVC_WIN_VDEC_ALL = 12`, 0x16000000+0x30000, `RWVC_WIN_COUNT` 13, page map + larb exception in the comment. Needed by DEC04 and T02's fake too. |
| `nord2_vcodec-vdec-all.patch` | **integrated by hw-k02** in `kern/bsd/dev/nord2_vcodec.inc` (their sha 5c9fddfb at the time) and extended there with a second allow-list entry, larb4 0x2e000 offset 0x00c bit 0, from DEC04/opus-dec2's trace. Kept for the record; `run_tests.sh` tests the tree file directly when it already carries `n2vc_vdec_page_blk`, and only applies this patch to a tree that does not. |
| `n2vc_hw_fake-vdec-all.patch` | **pending**: posted to hw-t02. The `VDEC_ALL` window (0x30000 backing array), per-page power, the same two-entry bit-masked larb rule, script name. Needs header rev 3. |
| `t_fake-vdec-all.patch` | **pending**: posted to hw-t02. Two table entries so T02's generic window loop covers the window. |

All four apply to the tree files as of 2026-11-11 ~17:35; `run_tests.sh` fails with `patch <name> is stale` if a tree
file moves afterwards.
