/* SPDX-License-Identifier: MIT */
# R2 — review findings, tracks K, J, P, D + rule compliance

Reviewer: hw-r2. Started 11 November 2026. Host-only review: no phone, no publishing, no commits.
Scope: every package of tracks K, J, P and D (`work_packages.json`), against its package entry, the
frozen headers in `gpu/hwcodec/include/`, and the Linux/phone-verified oracle the kernel code cites.

Method:

1. read the package source against its `work_packages.json` entry, the frozen header and the Linux
   source line cited for every register sequence (`K = nord2-current-kernel`);
2. run its own `run_tests.sh` from the work-tree root (all products under `/tmp`);
3. check the safety rules of the kernel model: bounded waits, no block access before its domain is
   on and unprotected, only this driver's bits in shared registers (SPM, infracfg, topckgen, LARB),
   user pointer/size validation, integer overflow in size arithmetic, locking, close/killed-client
   behaviour, physical ranges inside the claims of `include/redwood_vcodec.h`;
4. check the P/J interfaces against the frozen protocol (`include/n2vcd_proto.h`) and the VT ABI
   (`gpu/video/include/n2vt_abi.h`), and check FACTS.md evidence quality;
5. rule-compliance sweep every ~45 minutes: files >300 kB, binaries/vendor copies in the tree,
   `git status` outside owned paths, missing SPDX lines.

Snapshot: the tree is **being edited while this review runs** (K02/IK are being patched by the
opus-kern audit, IP is implementing the lead's exit-70 rule, P03/P04/J04 are new workers). The
verdicts below are for the named DONE revision; a "current tree" row records what changed after it.

## Verdicts

| pkg | DONE rev reviewed | verdict | evidence |
|---|---|---|---|
| K01 | **no DONE post** (STATUS 11:45) | **accept with findings** — implementation is sound for NV12; YUYV is unverified, one wrong chroma address at the maximum size | `kern/test_k01/run_tests.sh` → `N2TEST K01 PASS 822/822 (violations 0)` ×3 (ASan+UBSan, -O2, TSan); kernel-compiled by IK's staged check. `jpegdev.inc` sha256 `b57f87f9…aa98`. |
| K02 | DONE rev 3 (sha `a939f107…`) | **accept** — rev 3 contains the fixes for F1-F4 and the opus-kern audit: 0x3000 VENC windows, MDP bit-19 + `CLK_CFG_1` gate checks for larb8/larb4, `n2vc_slot_close_all()` (one `n2vr_server_close` per open), larb7/8 `0xfff01` (no MMSRAM), sleeping waits with per-open-id revalidation, `RWVC_DMA_INFO`, 1 MiB chunked DMA_RW. I read the final file; the VENC IRQ GICD ISPENDR probe is read-only. Hand-off still needs IK to regenerate 0299 from this sha and `compile_check.sh` to print `0 unknown` (integration, not K02). | `kern/test_k02/run_tests.sh` → `N2TEST K02 PASS 588871/588871 (violations 0)` ×2 on `a939f107`; K02's staged compile of the full `sweet_touch.c` is clean (1,318,688-byte object, 0 unknown undefined). |
| IK | DONE rev 3 + final tree (relay `a2e90ed5…`) | **accept** — the final relay adds process references, `n2vr_reap()` and `RWVC_DMA_INFO`; the suite now covers those paths (1479 checks vs 1441). F6 is closed; F7/F8 are low. The regenerated 0299 embeds the exact final files and compiles with 0 unknown undefined. | `kern/test_ik/run_tests.sh` → `N2TEST IK PASS 1479/1479` ×2, `0299: files 6 lines 2730 bytes 117119`, `KERNEL COMPILE OK, new undefined symbols: 0 unknown`, `N2TEST IK PASS 4439/4439`; 0299 sha `e04aa289…`, embedded vcodec `a939f107`, relay `a2e90ed5`, jpeg `b57f87f9`. |
| J02 | DONE | **accept with findings** — client library, tool and VT object match the frozen ABI; only low items (start-session accepts sizes the kernel rejects, attribute-failure path, scratch memory) | `jpeg/test_j02/run_tests.sh` → `N2TEST J02 PASS 1483/1483`, including `t_rwjpeg` 607/607, `t_vt_hw` 78/78 and three arm64-ios16 compile checks. |
| J04 | DONE | **accept with findings (F30/F31)** — parser + word generator + tool + 52-JPEG oracle suite are real; 11 words [V], 9 slots + 4 index pointers remain [I], and hardware decode is not proven without the vendor-library host capture (the STATUS says so). Do not present hardware JPEG decode as done. | `jdec/run_tests.sh` → `N2TEST J04 PASS 5296/5296` (2639 checks ×2, ASan+UBSan/-O2, djpeg/ffprobe oracle, freestanding + arm64-ios16 compile). |
| IP | DONE rev 4 + current tree | **accept with one high finding (F15)** — F14 is fixed: guest stop answers E_GUEST, powers off, logs and `_exit(70)`; plist `RunAtLoad=true`, `KeepAlive={SuccessfulExit:false}`, `ThrottleInterval=10`, `--gate` marker. `RWVC_DMA_INFO` is used. 722/722 in three builds. **F15 method fixed**: IV removed the weak tables; `build_ios.sh` extracts the archive objects and excludes the fake. The product link now fails only on `_pthread_key_create`/`_pthread_setspecific`/`_strnlen` missing from `daemon/sdk_symbols.txt`; add them and the 271504-byte vcodecd links. | `daemon/test/run_tests.sh` → `N2TEST IP PASS 722/722`; `build_ios.sh` → `IOS BUILD OK, vcodecd 254208 B`;  |
| P03 | DONE rev 1 | **accept** — client library, relay/socket transports, hardware back end and CLI read; protocol framing, seq/session checks, bounds and cleanup are sound. F27-F29 were found and fixed in rev 2. | `client/test/run_tests.sh` → `N2TEST P03 PASS 550/550` (171/171 ×3 builds, CLI 12/12, arm64-ios16 compile); P03 reports its CLI against IP's daemon: `N2TEST IP PASS 717/717`. |
| P04 | DONE rev 1 | **accept with findings (F32/F33)** — annexb/avcC/hvcC match ffmpeg byte for byte, the VT object matches `n2vt_abi.h`, the default client seam links and runs against P03; registration hook not applied, -12915 fallback is [I] (documented; switch-file mitigation recommended). | `vt/test/run_tests.sh` → `N2TEST P04 PASS 5029/5029` (725 ×2 annexb, 1193 ×3 object, 4/4 arm64-ios16). |
| D01 | DONE rev 3 | **accept with findings** — FACTS is complete and every row has evidence + a mark; two factual defects (infracfg SET/CLR offsets; 4-vs-5 count) | `facts/run_tests.sh` → `N2TEST D01 PASS 32/32`; FACTS.md read line by line. |

## Findings

Status: `open`, `fixed`, `rejected`. Severity: `high` (phone hang/corruption, wrong data, or an
integration blocker), `medium` (architectural/ABI deviation, unverified reachable path), `low`.

| id | pkg | sev | status | one line |
|---|---|---|---|---|
| F1 | K02 | high | **fixed in rev3 (a939f107)** | VENC/VENC_C1 windows are 0x2000 but the vendor maps 0x3000; offsets in the third page are rejected |
| F2 | K02 | high | **fixed in rev3 (a939f107)** | no MDP-domain check before releasing larb8/larb4 bus protection (camera hang pattern) |
| F3 | K02 | high | **fixed in rev3 (a939f107)** | `d_close` is once per vnode, not per open; rev2 frees one slot, so two minor-0 opens leak and `server_opens` sticks |
| F4 | K02 | medium | **fixed in rev3 (a939f107)** | VENC larb NON_SEC_CON bits 16-19 (MMSRAM routing) are not cleared; MMSRAM is not powered |
| F5 | K02 | low | open | a hardware ioctl on the relay's second minor-0 fd is dispatched to the first slot (same proc, different open) |
| F6 | IK | high | **fixed (final relay, 1479 checks)** | XNU calls `d_close` only at the last close of the node anywhere, so per-process close notices do not fire for an earlier client; rev3's `opens` counter cannot fix it |
| F7 | IK | low | open (accepted) | `n2vr_reap()` → `n2vc_relay_closed()` runs block power-off waits while holding `n2vr_lock` |
| F8 | IK | low | open (accepted) | `proc_find(proc_pid(p))` may return NULL in `n2vr_client_open`; the slot then keeps a raw `struct proc *` with no reference |
| F9 | K01 | medium | open | YUYV writes `SRC_CHROMA_ADDR = src + stride*rows`; at 4096x3065..3072 that equals the output buffer's PA. Linux passes `fb_addr[1]` (0 for single-plane YUYV), not a computed address |
| F10 | K01 | low | open (header issue, hw-lead) | `struct rwj_info` is 60 bytes and `rwj_decode` 68, contradicting `redwood_jpeg.h`'s "every struct is a multiple of 8 bytes" |
| F11 | J02 | low | open | `enc_start_session` accepts 2..8192 even sizes, while `/dev/redwood-jpeg` requires >=16 and `w*h <= 12.58 MP`; the session starts and every frame then fails |
| F12 | J02 | low | open | if `VTEncoderSessionSetPixelBufferAttributes` fails, the session is still recorded as started |
| F13 | J02 | low | open | `rwjpeg_insert_segments` sums `4 + size` over caller-controlled `n` without a bound; a `size_t` overflow is unreachable in practice but the check is missing |
| F14 | IP | high | **fixed (722/722 test)** | a guest stop must answer E_GUEST, power off, log and `_exit(70)`; rev4 keeps serving, and the plist has `KeepAlive=false` so nothing restarts it |
| F15 | IP | high | **fixed in method; product link blocked on 3 TBD symbols** | the iOS link resolves the weak `n2shim_thread`/`n2shim_stdio` definitions from `n2vhost.o` and never pulls V32/V33 from `libn2vhost.a`; the phone daemon would run with empty tables (opus-ios's evidence) |
| F16 | IP | medium | open | `session_end()` drops VENC refs through `g.hw`, while V41/IV use `g.vhw`; it works only because IV's wrapper forwards, and bypasses the wrapper's accounting |
| F17 | IP | medium | open | no idle/session timeout: a live client that opens and then stops keeps the only session and its power references forever |
| F18 | IP | medium | open | `register_dma_handle` caps a session at 16 client handles; a client with a larger buffer pool gets E_ARGS on the 17th |
| F19 | IP | low | open | `handle_enc_frame` uses the client-claimed `out_capacity` for `enc_cap`/the SPS-PPS shift; the kernel bounds the real DMA, so this is a misleading-failure risk, not corruption |
| F20 | IP | low | open | `handle_enc_set` does not bound `fps_num`/`fps_den` (OPEN does); `in_handle == out_handle` is not rejected |
| F21 | IP | low | open | `build_ios.sh` compiles products with `-Wall -Wextra` but not `-Werror`; the tests use `-Werror` |
| F22 | D01 | medium | open | FACTS 1.10 labels the infracfg registers wrongly: it says `MM SET 0x2D0 CLR 0x2D4`; the Linux names are `PROTECTEN_MM` 0x2D0, `_SET` 0x2D4, `_CLR` 0x2D8 (and MM_2 0xDC8/_SET 0xDCC/_CLR 0xDD0). K02 uses the correct offsets |
| F23 | D01 | low | open | FACTS 4.11 says "4 unclassified mnemonics" and lists 5 (`smmul`, `uxtb16`, `aese.8`, `sha1su1.32`, `sha256su0.32`); STATUS.md repeats it |
| F24 | D01 | low | open | rows marked `[V]/[I]` (1.17, 1.22, 5.6, 6.4) mix verified and inferred claims; split them |
| F25 | D01 | low | open | FACTS 1.7 gives `IMG_STRIDE = align(even(w),16)` only; the Linux code uses `align(width*2, 32)` for 4:2:2 (`mtk_jpeg_core.c:1179`). Add the 4:2:2 form |
| F30 | J04 | medium | open (documented) | 9 of the 21 `data[]` slots and all 4 index pointers are hypotheses; without the vendor-library host capture, hardware JPEG decode is not proven feasible from the open sources |
| F31 | J04 | low | open (documented) | `data[12]` (file size vs ION allocation size) and the 3-table index allocation are inferences |
| F32 | P04 | medium | open | the fallback status -12915 is [I]: the VT selection loop's retry set was not verified; a missing daemon may fail the session instead of selecting software |
| F33 | P04 | low | open | the registration hook and the four `build_ios.sh` compile lines are not applied; the objects are compiled, not linked into `Slim.videoencoder` |
| F27 | P03 | low | **fixed (rev 2, 550/550)** | after a successful OPEN_OK, a `calloc` failure in `n2vcd_enc_open` returns without sending CLOSE; the header promises no server session is left behind |
| F28 | P03 | low | **fixed (rev 2, 550/550)** | `xact()` does not check the reply header's `session`; each caller checks it separately (defense in depth) |
| F29 | P03 | low | **fixed (rev 2, 550/550)** | the relay transport does not probe `RWVC_INFO.abi_version` (the hardware back end does); a mismatched kernel fails at the first request instead of a clear version error |
| F26 | P03 | low | fixed rev1 | `client/n2vcd_client.h` says "vcodecd exits after a guest stop" (repeated from the daemon comment) but the client-side rule should be the lead's: retry a fresh connect at the next StartSession; also fix the header's claim that `RWVC_DMA_SHARED` is a frozen macro (it is now, rev 2, so this is only a comment sync) |

The findings F1-F6 are the same defects independently found by **opus-kern** in its 16:08 audit; I
verified them against `K` and against the current file rather than copying the report. F14/F15 are
the lead's / opus-ios's reports; I verified the plist and the weak-symbol mechanism.

### F1 — K02, high (fixed in rev3, a939f107)

`kern/bsd/dev/nord2_vcodec.inc` rev2 `n2vc_wins[]`: `{0x17020000, 0x2000, VENC}`,
`{0x17820000, 0x2000, VENC_C1}`. `host/PROGRESS.md:111-117` records the real daemon's mmap:
`0x17020000 len 0x3000`, `0x17820000 len 0x3000`. `n2vc_op_window()` rejects
`offset > bytes - 4`, so any access to 0x2000..0x2FFC is `EINVAL`; the vendor asked for that page
deliberately. The 16:08 file uses 0x3000; 0x17023000 still stops below JPGENC at 0x17030000, so
there is no overlap with K01.

### F2 — K02, high (fixed in rev3, a939f107)

`smi_drv.c:198-232` (MT6885/MT6893): larb 7 and 5 prepare the **DISP** common (units 21/24/25);
larb 8 and 4 prepare the **MDP** common (units 22/26/27, `mdp_smi_common@1f002000`, iommu1 at
0x1f027000). `n2vc_hw_on()` rev2 checks only the DIS status bit (20) before releasing protection.
For VENC_C1 and VDEC_CORE that is the camera bring-up's hang pattern (`nord2_seninf.inc:152`:
system lost 27-44 s after releasing a domain whose bus parent was off). The 16:08 file adds
`N2VC_MDP_BIT (1 << 19)` and refuses VENC_C1/VDEC_CORE with EACCES before any write; the K02 host
model fails the run on an access with MDP off and now prints the refusal in its expected-fault
cases. This is the finding that must not be lost.

### F3 — K02, high (fixed in rev3, a939f107)

`bsd/miscfs/specfs/spec_vnops.c:330-353` calls `d_open` on **every** open; `:2760` calls `d_close`
only when `vcount(vp) == 0` (no `D_TRACKCLOSE` in this XNU). Rev2 `n2vc_dev_close()` freed one slot
and called `n2vr_server_close()` once. vcodecd itself opens minor 0 **twice**: once in
`n2vc_hw_open_dev()` (`vcodecd.c:1117`) and once in `n2vcd_srv_open_relay()`
(`n2vcd_srv.c:84`). So one close frees one slot, the other `n2vc_open` (and any power refs/DMA it
holds) leaks, `n2vc.files` can exhaust after 16 opens, and `n2vr.server_opens` never reaches 0
(`server_present` stays 1 after the daemon exits). The 16:08 file has `n2vc_slot_close_all()`: it
destroys every slot of the minor, marks busy ones dead for the ioctl path, and calls
`n2vr_server_close()` once per slot found. The host test still needs a case with two minor-0 opens
and a close order that leaves one busy.

### F4 — K02, medium (fixed in rev3, a939f107)

`mtk_vcodec_enc_pm.c:305-320` calls `smi_sysram_enable()` for VENC ports 5, 6, 13, 14, 21, 22;
`smi_drv.c:498-524` sets NON_SEC_CON bits 16-19 (`0xf << 16`) to route them to MMSRAM. K02 rev2
cleared only bits 0 and 8-15 (`0xff01`). If the boot chain left 16-19 set, those ports address
MMSRAM, which K02 never powers. The 16:08 file clears `0xfff01` on larb7/8 and keeps `0xff01` on
larb4/5; vcodecd must pass `slbc_ready = 0` (P03/AP side) for this to stay true.

### F5 — K02, low

`n2vc_dev_ioctl()` finds the first non-dead slot of the minor, not the slot of the descriptor that
issued the ioctl. XNU gives no per-open pointer, so this cannot be perfect; with vcodecd's two
minor-0 fds, a hardware ioctl on the transport fd would be accounted to the hardware fd. All block
power is global and refs are released at the last close, so the practical effect is limited to
per-file ref accounting. Document the single-server assumption in the header/STATUS.

### F6 — IK, high (addressed in the 16:08 working tree, not yet tested)

Rev3's `n2vr_client_open()` counted `opens` per process and `n2vr_client_close()` fired a notice
when it reached zero. With XNU's `d_close` only at the last close of the vnode (`spec_vnops.c:2760`),
a second client process that exits never produces a close; the first client's slot stays OPEN
forever and its K02 state (DMA handles, power refs) is never freed. With one client (the VT plug-in)
this works; with two it does not. The 16:08 file holds a `proc_find()` reference per slot and calls
`n2vr_reap()` from every relay entry, which turns a **dead process** into a normal close notice.
This is the right direction; it is not in any DONE post and `test_ik` is currently red, so it cannot
be accepted yet. Two residual limits: (a) a client that closes the node but stays alive is not
detectable — vcodecd must send CLOSE on `n2vcd_close()` and needs an idle timeout (F17); (b) the
reap's `proc_find`/`n2vc_relay_closed` path runs under `n2vr_lock`.

### F7 — IK, low

`n2vr_client_gone_locked()` calls `n2vc_relay_closed(p)` while `n2vr_lock` is held. K02's
`n2vc_open_destroy()` → `n2vc_hw_off()` can wait for protection/SRAM acks (bounded, tens of ms), so
one client's death blocks every other client and the server for that time. Acceptable for v1 but
worth a comment; a two-phase (snapshot under the lock, clean up after) needs the proc reference that
the 16:08 code already holds.

### F8 — IK, low

`n2vr_client_open()` does `struct proc *ref = proc_find(proc_pid(p));` and, if it returns NULL,
still stores `c->proc = p` with `c->ref = NULL`. The code comment says the reference keeps the
pointer valid until the slot frees; with no reference, a later `n2vc_relay_closed(c->proc)` can use
a recycled `struct proc`. `proc_find` on the current process during `d_open` should not fail, but
refuse the open (EBUSY) if it does.

### F9 — K01, medium

`nord2_jpegdev.inc:377` writes `SRC_CHROMA_ADDR = src_pa + hw_stride * rows`. Linux's
`mtk_jpeg_enc_set_config()` (`mtk_jpeg_enc_hw.c:207-221`) uses `fb->fb_addr[0] + align_h *
mem_stride` only for the single-plane NV12/NV21 case, and for every other format (including YUYV)
uses `fb->fb_addr[1]`. In a single-plane YUYV buffer `fb_addr[1]` is not the luma plane plus a
computed offset. K01's host model *requires* the computed address (`t_jpegdev.c` checks
`chroma == luma + stride*rows` for YUYV too), so the test cannot catch this. For 4096-wide YUYV with
height 3065..3072 the computed value is exactly `N2JD_DST_PA`, i.e. the output buffer. Fix: write 0
(or `src_pa`) for YUYV and add a test that checks it; measure on the phone before claiming YUYV.

### F10 — K01, low (frozen-header defect, owner hw-lead)

`struct rwj_info` = 6×4 + 9×4 = 60 bytes; `struct rwj_decode` = 24 + 7×4 + 4×4 = 68. The header
prologue says "every struct is a multiple of 8 bytes". `RWJ_IOC` encodes the true sizes (60/68), so
the ABI works; the invariant text is wrong. Correct the comment or pad the structs in the next
header revision.

### F11-F13 — J02, low

F11: `n2jpeg_hw_vt.c:500` accepts `dims.width/height >= 2` and even, up to 8192; `/dev/redwood-jpeg`
(`redwood_jpeg.h:37`) requires >=16 and `w*h <= 4096*3072`. A 2x2 session succeeds at StartSession
and every `encodeFrame` returns -12912. Reject at StartSession with `N2_PROP_PARAM_ERR` (or scale),
and reject `w*h > RWJ_MAX_PIXELS`.
F12: `enc_start_session()` records `e->session`/width/height before
`VTEncoderSessionSetPixelBufferAttributes()`; if that call fails it returns the error but leaves the
session usable with possibly wrong attributes. Clear the session or set it only after success.
F13: `rwjpeg_insert_segments()` computes `insertion += 4 + segments[i].size` for caller-controlled
`n`; each size is <= 65533, so a 64-bit overflow needs ~2^48 segments, but a cheap `if (insertion >
SIZE_MAX - 4 - size) return RWJPEG_E_INVAL;` makes the check explicit.

### F14 — IP, high (the lead's rule; in progress in the 16:13 tree)

`vcodecd.c` rev4 documents and implements "keeps serving" after a guest stop. The lead's
11 Nov message is explicit: after a guest stop (or a thread-shim failure) answer the pending request
with E_GUEST + detail, send an ERROR to the other clients, power the blocks off (best effort,
bounded), log one fatal line with the stop reason/pc, then `_exit(70)`; hardware timeouts (E_HW) may
keep serving. The plist must then set `KeepAlive` true (launchd restarts) with
`ThrottleInterval >= 5 s`. At the 16:13 snapshot `fatal_guest_stop()` exists and is correct in
shape, but no call site uses it yet and the plist is still `KeepAlive=false`; the build will also
fail with `-Wunused-function` if it stays unused. P03's header comment ("vcodecd exits after a guest
stop") is right about the intended rule, P04 must treat E_GUEST as session-fatal and retry a fresh
connect at the next StartSession.

### F15 — IP, high (opus-ios; verified by construction)

`host/n2vhost.c:48-51` defines weak stand-ins for `n2shim_thread`, `n2shim_stdio`, `n2shim_android`
and `n2shim_data_all`. `n2vhost.o` is always pulled from `libn2vhost.a` (it defines
`n2vhost_start`), so its weak definitions satisfy the references and the archive members that
provide the real tables are never extracted. A plain `ld ... libn2vhost.a` produces a phone binary
whose `n2shim_thread`/`n2shim_stdio` are the empty stands-ins: 77 vendor imports unresolved, vpud
never reaches M-c. `build_ios.sh:174` and the IP-REAL link in `daemon/test/run_tests.sh` use that
plain link. Fix one of: (a) `-Wl,-force_load,"$VHOST_LIB"` (ld64) / `-Wl,--whole-archive …
-Wl,--no-whole-archive` (Linux); (b) IV drops the weak definitions and tests the address of an
extern weak declaration (the pattern already used for `n2shim_set`). Add a test assertion that the
`N2VHOST tables` line has no `=0(` for thread/stdio.

### F16-F21 — IP, medium/low

F16: `session_end()` calls `g.hw.power`; V41 was given `g.vhw` (`n2vhost_hw()`). IV's wrapper
forwards to the same callbacks, so the refs do drop, but using `g.vhw` for every AP-facing call
keeps accounting/ownership in one place and survives a wrapper that is not a pure forwarder.
F17: no idle timeout. The relay now detects dead processes, but a client that stays alive and stops
sending keeps the only session, the VENC power refs and the 4K hardware reserved indefinitely; add a
`--session-idle-ms` (e.g. 10 s) that ends an inactive session (E_HW/E_SESSION) before OPEN of
another client.
F18: `g.s.reg_handle[16]` and the `too many DMA handles` path; P03 uses two buffers, so this is not
a blocker today, but the V41 table is 256 entries and a VT client with a buffer ring would hit it.
F19: `enc_cap = req->out_capacity - HDR_RESERVE`; V41 passes the registered `bs_bytes` and the
kernel `RWVC_DMA_RW` bounds the real buffer, so a lying client cannot corrupt memory, but the
daemon should use the `bytes` returned by `n2vcd_srv_dma_info`/the registered handle for its own
bound and for the shift.
F20: `handle_enc_set()` divides caller fps values without the OPEN bounds (a huge `frm_rate` reaches
V41); and `in_handle == out_handle` is accepted.
F21: `build_ios.sh` uses `-Wall -Wextra` but not `-Werror`; the tests use `-Werror`, so a product
warning can slip through only in the one build the phone uses.

### F22-F25 — D01, medium/low

F22: FACTS.md 1.10 says "MM SET 0x2D0 CLR 0x2D4 STA1 0x2EC" and "MM_2 SET 0xDC8 CLR 0xDD0". The
Linux names (`clk-mt6885-pg.c:225-241`) are `PROTECTEN_MM` 0x2D0 (current/status), `_SET` 0x2D4,
`_CLR` 0x2D8; `PROTECTEN_MM_2` 0xDC8, `_SET` 0xDCC, `_CLR` 0xDD0; STA1 is 0x2EC/0xDD8. K02 and K01
use the correct offsets (0x2D4/0x2D8/0xDCC/0xDD0), so the bad row is a documentation hazard, not a
code defect. Rewrite the row as "MM 0x2D0 (set 0x2D4, clr 0x2D8, ack 0x2EC); MM_2 0xDC8 (set
0xDCC, clr 0xDD0, ack 0xDD8)".
F23: "The 4 unclassified mnemonics are" followed by five names. Count is 5.
F24: rows 1.17, 1.22, 5.6 and 6.4 carry `[V]/[I]`; split the verified part (file:line) from the
inference, because FACTS.md is the reference other workers read.
F25: 1.7 omits the 4:2:2 `IMG_STRIDE` rule (`mtk_jpeg_core.c:1177-1179`: `align(width*2, 32)`),
while K01 implements it. Add it; a reader using the row for YUYV would program 16-alignment.

### F26 — P03, low

`client/n2vcd_client.h` line 26 says "vcodecd exits after a guest stop; launchd restarts it" while
the lead's rule is that the **daemon** exits 70 and the **client** retries a fresh connect at the
next StartSession. The header already says the right client behaviour in the same paragraph; drop
the stale daemon claim. The `RWVC_DMA_SHARED` note is now correct with header rev 2.

## Rule-compliance sweep (16:10 snapshot)

Commands and results:

    $ find gpu/hwcodec -type f -size +300k -printf '%s %p\n' | sort -n
    ... only gpu/hwcodec/build/**, gpu/hwcodec/dsh-state/logs/**, gpu/hwcodec/runs/q10hw1/kernel.log
    $ find gpu/hwcodec -type f \( -perm -111 -o -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '*.dylib' \)
    ... only shell/python scripts that are meant to be executable; no .o/.a/.so/.dylib in worker dirs
    $ du -sh gpu/hwcodec/{kern,jpeg,daemon,client,vt,install,ap,vcu,host,review/r1}
    436K kern, 156K jpeg, 168K daemon, 8K client, 0 vt, 48K install, 260K ap, 176K vcu, 136K host, 148K review/r1

No worker-owned source directory exceeds 2 MB; no vendor library or disassembly listing is in the
tree; `kern/0299-*.patch` embeds the frozen headers and the three `.inc` sources, not vendor data.

Missing SPDX in K/J/P/D-owned files, checked as "line 1 or line 2" (a shebang may be line 1;
T01 correctly pointed out that its scripts carry the line after the shebang, and my first pass
only looked at line 1):

- `kern/compile_check.sh`, `kern/make_patch.sh`, `kern/test_ik/run_tests.sh` (IK)
- `kern/test_k01/run_tests.sh` (K01)

K02 rev 4 added the lines to its files, J02 and IP's scripts carry them, P03's files carry them.
These are harness scripts, not product code; all product `.c` and all kernel `.inc` files have
the line. Low.

Other sweep results:

- `gpu/hwcodec/runs/q10hw1/kernel.log` is 891 kB and `dsh-state/logs/*.jsonl` are up to 5 MB; both
  are harness/phone artifacts outside a worker's `owned_files`, not new package sources. The
  200 kB-log rule is about what a worker writes into the tree; flagging to hw-lead so the harness
  owner can decide, not posting a package violation.
- `git status` shows modifications outside `gpu/hwcodec` in `gpu/video/**`, `gpu/include`,
  `gpu/metal`, `gpu/src`, `gpu/tests`, `gpu/tools`, `NOTES.md` and `emu/n2a32_t16.c`. The first
  group belongs to the video/GPU sessions, not to a hwcodec package; `emu/n2a32_t16.c` is T01's
  announced application of hw-r1's V14 patch (owner V14 inactive), which the board recorded. No
  hwcodec worker is editing another package's files without a board note.
- 0299 is now current: `kern/0299-*.patch` sha `e04aa289…` embeds K02 `a939f107`, IK relay
  `a2e90ed5`, JPEG `b57f87f9` and both frozen headers byte-identical; `compile_check.sh` prints
  `0 unknown`. Verified by applying 0298+0299 to a fresh q10at3 stage and hashing the result.

## Tests run (exact last lines)

    bash gpu/hwcodec/kern/test_k01/run_tests.sh
    N2TEST K01 PASS 822/822 (violations 0)        x3 (ASan+UBSan, -O2, TSan)

    bash gpu/hwcodec/kern/test_k02/run_tests.sh
    N2TEST K02 PASS 588871/588871 (violations 0)  x2 (rev3, sha a939f107)

    bash gpu/hwcodec/kern/test_ik/run_tests.sh
    (current tree) 3 errors generated, -Werror: unused proc_kill/proc_reset in test_ik/t_relay.c
    (DONE rev3)    N2TEST IK PASS 1441/1441 x3; N2TEST IK PASS 4325/4325

    bash gpu/hwcodec/jpeg/test_j02/run_tests.sh
    N2TEST J02 PASS 1483/1483

    bash gpu/hwcodec/daemon/test/run_tests.sh
    N2TEST IP PASS 627/627        (15:55 snapshot, sha 63195953…; the 16:13 tree
                                   with the fatal-guest-stop edit does not compile yet:
                                   "error: unused function 'note_client'")

    bash gpu/hwcodec/client/test/run_tests.sh
    N2TEST P03 PASS 550/550

    bash gpu/hwcodec/jdec/run_tests.sh
    N2TEST J04 PASS 5296/5296

    bash gpu/hwcodec/vt/test/run_tests.sh
    N2TEST P04 PASS 5031/5031

    bash gpu/hwcodec/facts/run_tests.sh
    N2TEST D01 PASS 32/32

No test in this review uses the phone, adb/fastboot, the vendor files as a source copy, or the
network. Build products are under `/tmp/n2hw-R2`, `/tmp/n2hw-k01`, `/tmp/n2hw-k02`, `/tmp/n2hw-ik`,
`/tmp/n2hw-j02`, `/tmp/n2hw-ip` only.

## Limits of this review

- The kernel gate is green (K02 a939, IK a2e90ed5, 0299 e04aa289, compile 0 unknown). The one
  remaining high is IP F15 (weak-shim archive link); P04's registration hook and J04's [I] words are
  documented gaps, not regressions. This file will be re-issued as rev 1 after IP fixes F15.
- No phone run exists for any package; "host-tested" is the strongest claim here.
- I did not review V/T packages (R1's remit) except where a K/J/P behaviour depends on them.
- The K01 register model and the K02 SPM model share the driver's reading of the Linux source; they
  are not independent hardware evidence. The independent evidence is `runs/q10hw1` for VEN/JPEG and
  the Linux line citations for the rest.
