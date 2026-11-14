/* SPDX-License-Identifier: MIT */
/*
 * n2vhost.h -- the track-V host runner as a library: bring up the phone's own codec stack under the
 * AArch32 interpreter and keep it available to an AP-side driver (the encoder of track V, or the
 * daemon vcodecd of track P).
 *
 * What n2vhost_start() does, in this order (each step is reportable, see n2vhost_report()):
 *   1. create the guest address space (4 GiB reservation, V02) and the /dev/vcu model (V40) over the
 *      hardware back end it is given;
 *   2. register the shim tables in the order string, heap, thread, stdio, android, data (V30/V32/V33/V34);
 *   3. load bin/vpud and the four libraries of the phone's /vendor codec set from `vendor_dir` and link
 *      them against the shims (V03); imports that no table provides stay bound to the "unresolved" slot
 *      and are listed by the report;
 *   4. run the DT_PREINIT_ARRAY/DT_INIT/DT_INIT_ARRAY of every object (V03);
 *   5. start vpud's guest main on its own guest thread (one n2a32_cpu, one guest stack); the caller keeps
 *      the thread of n2vhost_start() and drives the AP side (n2ap_venc, V41) against the same n2vcu.
 *
 * Nothing here touches a phone: the hardware back end is struct n2vc_hw from include/n2vc_hw.h, the fake
 * one on the PC (n2vc_hw_open_fake, T02) and the /dev/redwood-vcodec one on the phone (P03).
 *
 * Milestones (the letters are those of gpu/hwcodec/work_packages.json package IV):
 *   M-a  the five vendor objects are loaded and linked; no unresolved import has been called yet
 *   M-b  the init arrays ran
 *   M-c  vpud reached its first ioctl on /dev/vcu
 *   M-d  the AP side's AP_IPIMSG_ENC_INIT was answered (n2ap_venc_open succeeded)
 *   M-e  the first register access to the VENC window was seen by the hardware back end
 *   M-f  the encode reached its wait for the done interrupt
 * n2vhost tracks M-a/M-b/M-c itself and takes M-d/M-e/M-f either from the n2vc_hw traffic it sees
 * (M-e, M-f) or from n2vhost_note() (M-d, and M-c if a caller can prove it another way).
 */
#ifndef N2VHOST_H
#define N2VHOST_H
#include <stdint.h>
#include <stdio.h>
#include "n2a32.h"
#include "n2elf.h"
#include "n2vc_hw.h"

struct n2vhost;
struct n2vcu;                       /* V40; only used through this pointer here, so this header does not
                                     * depend on vcu/n2vcu.h being present */

enum n2vhost_ms {
	N2VHOST_MS_NONE = 0,
	N2VHOST_MS_A, N2VHOST_MS_B, N2VHOST_MS_C, N2VHOST_MS_D, N2VHOST_MS_E, N2VHOST_MS_F
};

/* `vendor_dir` is the extracted /vendor (bin/vpud, lib/lib{vpud_vcodec,vcodecdrv,vcodec_utility,vcodec_oal}.so).
 * NULL means $N2HW_VENDOR_DIR, else "gpu/build/cam-vendor-20261109/vfs" relative to the current directory.
 * `hw` must stay valid until n2vhost_stop(); it is used through a wrapper (the original struct is not modified).
 *
 * Returns 0 when the set is loaded, linked, its init arrays ran and the guest main was started;
 * 1 when the set is loaded and linked but a guest stop happened before the end of that sequence
 * (`*out` is still valid and describes the stop; the first unmet need is in the report);
 * -1 on a hard error (bad directory, load or link failure, no memory).  *out is set in every case (also on
 * -1, so that n2vhost_error() and the report can be read); the caller owns it and calls n2vhost_stop().
 *
 * One set per process: n2vfs (V33) keeps the /dev/vcu device nodes it is given for the lifetime of the
 * process and has no unregister, so a second n2vhost_start() in the same process is refused with an error
 * that names the limitation.  n2vhost_stop() stops the guest and releases the host thread, but the guest
 * address space, the /dev/vcu model and the linked set stay allocated (parked, reachable) until exit --
 * freeing them would leave n2vfs with a dangling device.  The daemon starts once, at process start. */
int n2vhost_start(struct n2vhost **out, const struct n2vc_hw *hw, const char *vendor_dir);

/* Same, but with the guest entry point chosen explicitly: NULL or "start" is vpud's own _start
 * (-> __libc_init, the real daemon path); "apps_init" calls libvpud_vcodec.so's apps_init and
 * "service-venc" its _app_venc_service init function.  The two latter are diagnostics that answer
 * "what would vpud need next" while a bionic shim is still missing; the daemon passes NULL. */
int n2vhost_start_entry(struct n2vhost **out, const struct n2vc_hw *hw, const char *vendor_dir,
	const char *entry);

/* The /dev/vcu model created in step 1 (NULL if V40 is not in the build). */
struct n2vcu *n2vhost_vcu(struct n2vhost *h);

/* The hardware back end as the guest components should use it: a copy of the caller's struct with the
 * register/IRQ/DMA callbacks wrapped so that M-e/M-f can be observed. Never NULL after a successful start.
 * Pass this pointer (not the original) to n2vcu/n2ap_venc so that their traffic is counted. */
const struct n2vc_hw *n2vhost_hw(struct n2vhost *h);

struct n2a32_mem *n2vhost_mem(struct n2vhost *h);
struct n2elf_set *n2vhost_elf(struct n2vhost *h);

/* M-d and anything else the caller proves: record a milestone with one line of evidence (copied). */
void n2vhost_note(struct n2vhost *h, unsigned ms, const char *evidence);

/* Record why a milestone was *not* reached (printed instead of the default text; copied). */
void n2vhost_blocked(struct n2vhost *h, unsigned ms, const char *why);

/* Record the stop of the guest main (called by the guest thread itself at the end). */
void n2vhost_note_stop(struct n2vhost *h, enum n2a32_stop stop, uint32_t addr, uint32_t insn,
	uint64_t icount);

/* Wait up to timeout_ms for the guest main to end (0) or return -1 on timeout. The guest stop reason is
 * kept; n2vhost_report() prints it. */
int n2vhost_join(struct n2vhost *h, unsigned timeout_ms);

/* n2vhost_join(2000) and if the guest does not come back (stuck inside a host shim), detach it and
 * continue: the interpreter is then still running on another host thread and the process must exit
 * rather than wait for it. Idempotent.  Does not free the guest address space, the /dev/vcu model or the
 * linked set (n2vfs holds them for process lifetime; see n2vhost_start's comment). */
void n2vhost_stop(struct n2vhost *h);

/* Why the last guest run stopped, as text ("RETURN", "UNDEF", "NOIMPORT __libc_init", ...); never NULL. */
const char *n2vhost_stop_text(struct n2vhost *h);

/* The same reason as the interpreter's enum (N2A32_STOP_NONE while the guest is still running).  For a
 * caller that carries the reason in a binary protocol (the daemon's N2VCD_E_GUEST.detail) instead of
 * parsing the text. */
enum n2a32_stop n2vhost_stop_reason(const struct n2vhost *h);

/* The last hard error of n2vhost_start() ("load ...: ...", "link: ..."), "" when there is none. */
const char *n2vhost_error(struct n2vhost *h);

/* Milestone bookkeeping. */
unsigned n2vhost_milestone(const struct n2vhost *h);
const char *n2vhost_milestone_name(unsigned ms);         /* "M-a" .. "M-f", "none" */
const char *n2vhost_milestone_evidence(const struct n2vhost *h, unsigned ms);

/* The report: the N2VHOST lines of the runner (objects, unresolved imports, milestones, the first unmet
 * need) and, as its last line, "N2VHOST reached M-x: <evidence>". Returns the milestone number reached. */
int n2vhost_report(struct n2vhost *h, FILE *out);

/* Number of unresolved imports (bound to the unresolved host-call slot), as n2elf_link() counted them. */
unsigned n2vhost_unresolved(const struct n2vhost *h);

/* Also list every registered shim/data table entry as "N2VHOST shim <name>" in the report (used by
 * test_iv to compare the unresolved set against D01's import oracle). Off by default. */
void n2vhost_dump_shims(struct n2vhost *h, int on);

#endif
