/*
 * Built-in AMFI interface for SWEET.
 *
 * On Apple platforms AppleMobileFileIntegrity registers this interface and
 * provides entitlements through the closed CoreEntitlements library. SWEET
 * has neither, and code signing reaches it on every exec (ubc_cs_blob_add).
 *
 * Entitlements: when a code signature is added, its XML entitlements blob
 * (already checked against the code directory) is parsed into a
 * SweetEntitlements object (iokit/Kernel/arm/SweetEntitlements.cpp), set as
 * the blob's csb_entitlements, and every query is answered from it. DER-only
 * signatures get none. Code signature and page hash validation stay with the
 * kernel. Trust caches remain unavailable (no Image4), so the TrustCache
 * interface is left at version 0.
 *
 * Platform binaries: AMFI marks binaries whose cdhash is in a trust cache
 * as platform binaries from its vnode_check_signature MAC hook. Without
 * trust caches, the SWEET policy below marks every binary on the root
 * filesystem instead (the iOS system binaries are ad-hoc signed and are
 * platform binaries only through the static trust cache).
 *
 * Interfaces no kernel path uses on SWEET (they serve other Apple kexts)
 * panic with a clear message instead of being NULL.
 */
#if defined(ARM64_BOARD_CONFIG_SWEET)
#include <kern/debug.h>
#include <kern/startup.h>
#include <libkern/amfi/amfi.h>
#include <mach/kern_return.h>
#include <sys/codesign.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/ubc_internal.h>
#include <sys/vnode.h>
#include <security/mac_policy.h>

/* iokit/Kernel/arm/SweetEntitlements.cpp */
void *sweet_entitlements_create(const CS_GenericBlob *blob, size_t blob_len);
void *sweet_entitlements_copy_dict(const void *ent);
bool sweet_entitlements_copy_xml(const void *ent, CS_GenericBlob **out);
bool sweet_entitlements_bool(const void *ent, const char *name);
bool sweet_entitlements_string(const void *ent, const char *name, const char *value);
void *sweet_entitlements_copy_object(const void *ent, const char *name);
unsigned int sweet_entitlements_count(const void *ent);
extern void osobject_release(void *object);

/* boot-arg sweet_amfi_debug=1: log the entitlements attached to each signature. */
static TUNABLE(uint32_t, sweet_amfi_debug, "sweet_amfi_debug", 0);

/* The entitlements object of a process's main binary, or NULL. */
static void *
sweet_proc_entitlements(proc_t proc)
{
	struct cs_blob *blob = csproc_get_blob(proc);
	return blob != NULL ? csblob_os_entitlements_get(blob) : NULL;
}

static void
sweet_amfi_entitlements_invalidate(__unused void *os_entitlements)
{
	/* Queries already require a valid signature (csblob_get_entitlements). */
}

static void *
sweet_amfi_entitlements_as_dict(void *os_entitlements)
{
	return sweet_entitlements_copy_dict(os_entitlements);
}

static CEError_t
sweet_amfi_entitlements_query(__unused void *os_entitlements,
    __unused uint8_t cdhash[CS_CDHASH_LEN], __unused CEQuery_t query,
    __unused size_t query_length)
{
	panic("SWEET AMFI: CoreEntitlements queries are not implemented");
}

static bool
sweet_amfi_entitlements_get_transmuted(__unused void *os_entitlements,
    __unused const CS_GenericBlob **blob)
{
	return false;
}

static bool
sweet_amfi_entitlements_get_xml(void *os_entitlements, CS_GenericBlob **blob)
{
	return sweet_entitlements_copy_xml(os_entitlements, blob);
}

static bool
sweet_amfi_get_legacy_profile_exemptions(__unused const uint8_t **profile,
    __unused size_t *profile_length)
{
	return false;
}

static bool
sweet_amfi_get_udid(__unused const uint8_t **udid, __unused size_t *udid_length)
{
	return false;
}

static void *
sweet_amfi_query_context_to_object(__unused CEQueryContext_t ctx)
{
	panic("SWEET AMFI: CoreEntitlements query contexts are not implemented");
}

static kern_return_t
sweet_amfi_adjust_with_monitor(__unused void *os_entitlements,
    __unused const CEQueryContext_t ce_ctx, __unused const void *monitor_sig_obj,
    __unused const char *identity, __unused const uint32_t code_signing_flags)
{
	/* SWEET has no code signing monitor. */
	return KERN_NOT_SUPPORTED;
}

/*
 * Attach the parsed XML entitlements to a code signature being added. This
 * must happen in the vnode_check_signature MAC hook, like AMFI does: later,
 * converting a 4K-page signature to multilevel hashes drops the XML blob.
 * Nothing is granted when the blob is missing, unreadable or does not match
 * its code directory hash.
 */
static void
sweet_amfi_attach_entitlements(struct cs_blob *code_signing_blob)
{
	void *xml = NULL;
	size_t xml_len = 0;

	if (csblob_get_entitlements(code_signing_blob, &xml, &xml_len) != 0) {
		printf("SWEET AMFI: %s: entitlements do not match the code directory, granting none\n",
		    csblob_get_identity(code_signing_blob));
		return;
	}
	if (xml == NULL) {
		return;                 /* no XML entitlements */
	}
	void *ent = sweet_entitlements_create(xml, xml_len);
	if (ent == NULL) {
		printf("SWEET AMFI: %s: unreadable entitlements (%zu bytes), granting none\n",
		    csblob_get_identity(code_signing_blob), xml_len);
		return;
	}
	if (sweet_amfi_debug) {
		/* The test keys only exist in tools/mkinit.py test signatures. */
		printf("SWEET AMFI: %s: %u entitlements (test: bool %d string %d array %d)\n",
		    csblob_get_identity(code_signing_blob), sweet_entitlements_count(ent),
		    sweet_entitlements_bool(ent, "com.apple.private.sweet.test-bool"),
		    sweet_entitlements_string(ent, "com.apple.private.sweet.test-string", "hello"),
		    sweet_entitlements_string(ent, "com.apple.private.sweet.test-array", "two"));
	}
	csblob_os_entitlements_set(code_signing_blob, ent);    /* takes a reference */
	osobject_release(ent);
}

static kern_return_t
sweet_amfi_adjust_without_monitor(__unused void *os_entitlements,
    __unused struct cs_blob *code_signing_blob)
{
	/* Entitlements were attached in sweet_amfi_vnode_check_signature. */
	return KERN_SUCCESS;
}

static kern_return_t
sweet_amfi_query_boolean(const void *os_entitlements, const char *entitlement_name)
{
	return sweet_entitlements_bool(os_entitlements, entitlement_name) ? KERN_SUCCESS : KERN_DENIED;
}

static kern_return_t
sweet_amfi_query_boolean_with_proc(const proc_t proc, const char *entitlement_name)
{
	return sweet_amfi_query_boolean(sweet_proc_entitlements(proc), entitlement_name);
}

static kern_return_t
sweet_amfi_query_string(const void *os_entitlements,
    const char *entitlement_name, const char *entitlement_value)
{
	return sweet_entitlements_string(os_entitlements, entitlement_name, entitlement_value) ?
	       KERN_SUCCESS : KERN_DENIED;
}

static kern_return_t
sweet_amfi_query_string_with_proc(const proc_t proc,
    const char *entitlement_name, const char *entitlement_value)
{
	return sweet_amfi_query_string(sweet_proc_entitlements(proc), entitlement_name,
	           entitlement_value);
}

static kern_return_t
sweet_amfi_copy_object(const void *os_entitlements,
    const char *entitlement_name, void **entitlement_object)
{
	void *obj = sweet_entitlements_copy_object(os_entitlements, entitlement_name);
	if (entitlement_object) {
		*entitlement_object = obj;
	} else if (obj) {
		osobject_release(obj);
	}
	return obj ? KERN_SUCCESS : KERN_NOT_FOUND;
}

static kern_return_t
sweet_amfi_copy_object_with_proc(const proc_t proc,
    const char *entitlement_name, void **entitlement_object)
{
	return sweet_amfi_copy_object(sweet_proc_entitlements(proc), entitlement_name,
	           entitlement_object);
}

static const amfi_t sweet_amfi = {
	.OSEntitlements_invalidate = sweet_amfi_entitlements_invalidate,
	.OSEntitlements_asdict = sweet_amfi_entitlements_as_dict,
	.OSEntitlements_query = sweet_amfi_entitlements_query,
	.OSEntitlements_get_transmuted = sweet_amfi_entitlements_get_transmuted,
	.OSEntitlements_get_xml = sweet_amfi_entitlements_get_xml,
	/* CoreEntitlements is only used by the PMAP_CS code signing monitor. */
	.get_legacy_profile_exemptions = sweet_amfi_get_legacy_profile_exemptions,
	.get_udid = sweet_amfi_get_udid,
	.query_context_to_object = sweet_amfi_query_context_to_object,
	/* .TrustCache stays version 0: trust caches are unavailable without Image4. */
	.OSEntitlements = {
		.version = OSENTITLEMENTS_INTERFACE_VERSION,
		.adjustContextWithMonitor = sweet_amfi_adjust_with_monitor,
		.adjustContextWithoutMonitor = sweet_amfi_adjust_without_monitor,
		.queryEntitlementBoolean = sweet_amfi_query_boolean,
		.queryEntitlementBooleanWithProc = sweet_amfi_query_boolean_with_proc,
		.queryEntitlementString = sweet_amfi_query_string,
		.queryEntitlementStringWithProc = sweet_amfi_query_string_with_proc,
		.copyEntitlementAsOSObject = sweet_amfi_copy_object,
		.copyEntitlementAsOSObjectWithProc = sweet_amfi_copy_object_with_proc,
	},
};

__startup_func
static void
sweet_amfi_register(void)
{
	amfi_interface_register(&sweet_amfi);
	printf("SWEET AMFI: built-in interface registered (XML entitlements)\n");
}
STARTUP(EARLY_BOOT, STARTUP_RANK_MIDDLE, sweet_amfi_register);

/*
 * The signature reached this hook validated, so mark it CS_SIGNED like AMFI
 * does: exec copies the flag to the process, and csproc_get_blob() (csops
 * status, cdhash, platform and entitlement queries by process) ignores
 * processes without it. Every signed binary on the root filesystem is a
 * platform binary. New signatures (ubc_cs_blob_add passes its writable copy;
 * revalidation passes the read-only blob itself) get their entitlements here.
 */
/*
 * 0733 (jbts-20261115): HFS answers FileProvider's APFS fsctls ('On My iPhone' missing in Files / document picker).
 * iOS 16 FileProvider assumes APFS: e.g. fpfs_fget_parent_syncroot() = ffsctl(fd, 0x40084A4A = _IOR('J', 74, 8), buf, 0) [V disasm,
 * FileProvider.framework 20H392]. Our HFS hfs_vnop_ioctl() ends in "default: return ENOTTY" [V source], so every APFS ('J') query of
 * the file-provider stack fails with ENOTTY. Written from the description of DenialOfGravity/ios-redfin commit 101171d (same idea,
 * own code): the HFS vnode-op vector's vnop_ioctl slot is wrapped once at the first exec; when HFS returns ENOTTY for a group 'J'
 * command from fileproviderd / LocalStorageFileProvider / Files, a read-only command (IOC_OUT only, <= 64 bytes, buffer present) is
 * answered with zeros and 0, any other 'J' command with ENOTSUP. Everything else (other processes, other groups, any result
 * but ENOTTY) is passed through unchanged (redfin: answering launchd/keybagd/apfs_iosd broke boot).
 *   kern.n2hfsapfs  0 = pass-through (old behaviour), 1 = answer (default); boot-arg n2hfsapfs=N
 *   log "NORD2 HFSAPFS: <proc>[pid] cmd 0x.. ('J' n, len l, dir d) -> rc", first 16 distinct commands + every 64th call after
 */
#include <sys/ioccom.h>
#include <sys/vnode_if.h>
#include <sys/vnode_internal.h>
#include <sys/sysctl.h>
#include <pexpert/pexpert.h>
static int (*sweet_hfsapfs_orig)(void *);
static unsigned int sweet_hfsapfs_mode = 1;
static uint32_t sweet_hfsapfs_seen[16];
static unsigned int sweet_hfsapfs_nseen, sweet_hfsapfs_calls;
SYSCTL_UINT(_kern, OID_AUTO, n2hfsapfs, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_hfsapfs_mode, 0,
    "0733: HFS answers APFS 'J' fsctls of the file-provider stack: 0 off, 1 on");

static int
sweet_hfsapfs_watched(const char *n)
{
	return strcmp(n, "fileproviderd") == 0 || strncmp(n, "LocalStorageFile", 16) == 0 || strcmp(n, "Files") == 0;
}

static int
sweet_hfsapfs_ioctl(void *v)
{
	struct vnop_ioctl_args *ap = v;
	int rc = sweet_hfsapfs_orig(v), out = rc;
	u_long cmd = ap->a_command;
	char name[32];

	if (rc != ENOTTY || sweet_hfsapfs_mode == 0 || IOCGROUP(cmd) != 'J') {
		return rc;
	}
	proc_selfname(name, sizeof(name));
	if (!sweet_hfsapfs_watched(name)) {
		return rc;
	}
	if ((cmd & IOC_DIRMASK) == IOC_OUT && IOCPARM_LEN(cmd) <= 64 && ap->a_data != NULL) {
		bzero(ap->a_data, IOCPARM_LEN(cmd));
		out = 0;
	} else {
		out = ENOTSUP;
	}
	unsigned int calls = __sync_add_and_fetch(&sweet_hfsapfs_calls, 1);
	int fresh = 1;
	for (unsigned int i = 0; i < sweet_hfsapfs_nseen && i < 16; i++) {
		if (sweet_hfsapfs_seen[i] == (uint32_t)cmd) {
			fresh = 0;
		}
	}
	if (fresh && sweet_hfsapfs_nseen < 16) {
		sweet_hfsapfs_seen[sweet_hfsapfs_nseen++] = (uint32_t)cmd;
	}
	if (fresh || (calls % 64) == 0) {
		printf("NORD2 HFSAPFS: %s[%d] cmd 0x%lx ('J' %lu, len %lu, dir 0x%lx) -> %d (call %u)\n", name, proc_selfpid(), cmd,
		    (unsigned long)(cmd & 0xff), (unsigned long)IOCPARM_LEN(cmd), (unsigned long)(cmd & IOC_DIRMASK), out, calls);
	}
	return out;
}

static void
sweet_hfsapfs_install(struct vnode *vp)
{
	static int done;
	unsigned int v;
	int (**ops)(void *);
	mount_t mp;

	/* the root volume's regular-file op vector is hfs_vnodeop_p (a local symbol of the HFS objects): take it from a signed
	 * binary's vnode on the HFS root, once */
	if (done || vp == NULL || vnode_vtype(vp) != VREG || (mp = vnode_mount(vp)) == NULL || !(vfs_flags(mp) & MNT_ROOTFS)) {
		return;
	}
	if (strncmp(vfs_statfs(mp)->f_fstypename, "hfs", MFSNAMELEN) != 0) {
		static int told;
		if (!told++) {
			printf("NORD2 HFSAPFS: root fs is '%s', not hfs: not installed\n", vfs_statfs(mp)->f_fstypename);
		}
		return;
	}
	if (!__sync_bool_compare_and_swap(&done, 0, 1)) {
		return;
	}
	if (PE_parse_boot_argn("n2hfsapfs", &v, sizeof(v))) {
		sweet_hfsapfs_mode = v;
	}
	ops = vp->v_op;
	if (ops == NULL || ops[vnop_ioctl_desc.vdesc_offset] == NULL) {
		printf("NORD2 HFSAPFS: no HFS vnode-op vector, not installed\n");
		return;
	}
	sweet_hfsapfs_orig = ops[vnop_ioctl_desc.vdesc_offset];
	ops[vnop_ioctl_desc.vdesc_offset] = sweet_hfsapfs_ioctl;
	printf("NORD2 HFSAPFS: HFS vnop_ioctl wrapped (slot %d), mode %u\n", vnop_ioctl_desc.vdesc_offset, sweet_hfsapfs_mode);
}

/*
 * 0734 (jbts-20261115): the root volume reported to statfs()/getfsstat()/getattrlist() as "apfs" once fileproviderd starts.
 * [V unified log, 43ts+fplog1 boot 647978C1] fileproviderd: "[INFO] Non-eligible device /dev/ufsroot0 [16777216]: non-apfs";
 * [V disasm 20H392] -[FPDVolume isEligibleForFPFS:] takes the getfsstat64 entry and requires MNT_LOCAL, !MNT_RDONLY and
 * strcasecmp(f_fstypename, "apfs") == 0; -[FPDVolumeManager loadSingleVolume:role:] skips a non-eligible volume ("Volume %s not
 * eligible to store FP library, skipping"), so no FileProvider library exists and the LocalStorage ("On My iPhone") domain is never
 * imported. XNU's own per-mount type-name override (vfs_mount_override_type_name, MNTK_TYPENAME_OVERRIDE: statfs/getattrlist only;
 * the kernel keeps using vfc_name "hfs") is set on the root mount when the first fileproviderd binary is exec'd -- after launchd's
 * boot tasks (fsck / mount -uw choose their helper from the type name). Global for statfs readers from then on.
 *   kern.n2fsapfs  1 = do it (default), 0 = never / write 0 at run time = report "hfs" again (override set back to "hfs")
 *   boot-arg n2fsapfs=0 disables.  Log "NORD2 FSAPFS: ...".
 */
static unsigned int sweet_fsapfs_mode = 1;
static int sweet_fsapfs_done;
static mount_t sweet_fsapfs_mp;
static int
sweet_fsapfs_sysctl(__unused struct sysctl_oid *oidp, __unused void *arg1, __unused int arg2, struct sysctl_req *req)
{
	unsigned int v = sweet_fsapfs_mode;
	int error = sysctl_handle_int(oidp, &v, 0, req);
	if (error || req->newptr == USER_ADDR_NULL) {
		return error;
	}
	sweet_fsapfs_mode = v ? 1 : 0;
	if (sweet_fsapfs_mp != NULL) {
		vfs_mount_override_type_name(sweet_fsapfs_mp, v ? "apfs" : "hfs");
		printf("NORD2 FSAPFS: root type name now reported as %s (sysctl)\n", v ? "apfs" : "hfs");
	}
	return 0;
}
SYSCTL_PROC(_kern, OID_AUTO, n2fsapfs, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, NULL, 0, sweet_fsapfs_sysctl, "I",
    "0734: report the HFS root as apfs to statfs once fileproviderd runs (0 off)");

static void
sweet_fsapfs_check(struct vnode *vp)
{
	char path[256];
	int len = sizeof(path);
	mount_t mp;
	unsigned int v;

	if (sweet_fsapfs_done || vp == NULL || vnode_vtype(vp) != VREG || (mp = vnode_mount(vp)) == NULL ||
	    !(vfs_flags(mp) & MNT_ROOTFS) || vn_getpath(vp, path, &len) != 0) {
		return;
	}
	len = (int)strlen(path);
	if (len < 14 || strcmp(path + len - 14, "/fileproviderd") != 0 || !__sync_bool_compare_and_swap(&sweet_fsapfs_done, 0, 1)) {
		return;
	}
	if (PE_parse_boot_argn("n2fsapfs", &v, sizeof(v))) {
		sweet_fsapfs_mode = v ? 1 : 0;
	}
	if (strncmp(vfs_statfs(mp)->f_fstypename, "hfs", MFSNAMELEN) != 0) {
		printf("NORD2 FSAPFS: root fs is '%s', nothing done\n", vfs_statfs(mp)->f_fstypename);
		return;
	}
	sweet_fsapfs_mp = mp;
	if (sweet_fsapfs_mode) {
		vfs_mount_override_type_name(mp, "apfs");
	}
	printf("NORD2 FSAPFS: %s exec'd: root type name reported as %s\n", path, sweet_fsapfs_mode ? "apfs" : "hfs (disabled)");
}

static int
sweet_amfi_vnode_check_signature(struct vnode *vp, __unused struct label *label,
    __unused cpu_type_t cpu_type, struct cs_blob *cs_blob,
    unsigned int *cs_flags, __unused unsigned int *signer_type,
    __unused int flags, __unused unsigned int platform,
    __unused char **fatal_failure_desc, __unused size_t *fatal_failure_desc_len)
{
	mount_t mp = vnode_mount(vp);
	*cs_flags |= CS_SIGNED;
	sweet_hfsapfs_install(vp);                      /* 0733: once, HFS is registered long before the first exec */
	sweet_fsapfs_check(vp);                         /* 0734 */
	{                                               /* 0731: which file-provider binaries get loaded at all */
		extern int sweet_sblog_sig(struct vnode *vp);
		(void)sweet_sblog_sig(vp);
	}
	if (mp != NULL && (vfs_flags(mp) & MNT_ROOTFS)) {
		*cs_flags |= CS_PLATFORM_BINARY;
	}
	if (cs_blob != cs_blob->csb_ro_addr && cs_blob->csb_entitlements == NULL) {
		sweet_amfi_attach_entitlements(cs_blob);
	}
	return 0;
}

/*
 * Policy "Sandbox": libsystem_sandbox calls __mac_syscall("Sandbox", op,
 * arg). Without a policy of that name every call fails with ENOPOLICY and
 * launchd reports mach lookups as "error = 159: Sandbox restriction"
 * (backboardd cannot reach mobilegestalt or iohideventsystem; tccd:
 * "profile not found"). SWEET runs without a sandbox: operations succeed
 * (checks allow, profile init is accepted); the few that must return
 * data (container paths; extension tokens until 0288) fail with ENOENT. The first
 * use of each operation number is logged.
 */
static uint64_t sweet_sandbox_seen;
static void sweet_trace_sandbox_check(struct proc *p, user_addr_t arg);     /* 0277e, below */

/*
 * 0277v: "is this process sandboxed?" for BlastDoor. libsystem_sandbox's sandbox_check(pid, NULL, 0) passes call 2 the arguments
 * { u64 *result, u64 pid, u64 operation = NULL, ... } and returns *result != 0; the stand-in above writes nothing, so every
 * process looks unsandboxed. MessagesBlastDoorService (imagent's "BlastDoor" unpacker for every incoming SMS/iMessage) refuses to
 * run so: "BlastDoor/Sandbox.swift:38: Fatal error: BlastDoor cannot run unsandboxed." (SIGTRAP at start, q10cl: 6 crashes,
 * imagent "Unpack failed with explosion ... ServiceCrashed", the 3 MT SMS that CommCenter decoded and acknowledged never reached
 * Messages). Only processes whose name holds "BlastDoo" (p_comm keeps 16 characters) are answered "sandboxed"; every other
 * question keeps the permissive answer. Boot-arg n2bb bit 29 turns it off.
 */
#include <pexpert/pexpert.h>
static int sweet_sandbox_blastdoor = -1;           /* -1 not decided yet */
static unsigned sweet_sandbox_blastdoor_logged;

static int
sweet_name_has(const char *s, const char *w)
{
	size_t n = strlen(w);
	for (; *s; s++) {
		if (strncmp(s, w, n) == 0) {
			return 1;
		}
	}
	return 0;
}

static void
sweet_sandbox_self_check(struct proc *p, user_addr_t arg)
{
	uint64_t a[3], one = 1;
	char subject[32];

	if (sweet_sandbox_blastdoor < 0) {
		uint32_t flags = 0;
		sweet_sandbox_blastdoor = !(PE_parse_boot_argn("n2bb", &flags, sizeof(flags)) && (flags & (1u << 29)));
	}
	if (!sweet_sandbox_blastdoor || arg == USER_ADDR_NULL || !proc_is64bit(p) || copyin(arg, a, sizeof(a)) != 0 ||
	    a[0] == 0 || a[2] != 0 || a[1] == 0 || a[1] > (uint64_t)INT32_MAX) {
		return;
	}
	subject[0] = '\0';
	proc_name((int)a[1], subject, sizeof(subject));
	if (!sweet_name_has(subject, "BlastDoo")) {
		return;
	}
	if (copyout(&one, (user_addr_t)a[0], sizeof(one)) == 0 && sweet_sandbox_blastdoor_logged++ < 8) {
		printf("SWEET Sandbox: %s[%d] answered \"sandboxed\" (0277v, BlastDoor)\n", subject, (int)a[1]);
	}
}

/*
 * 0288: sandbox extensions. The stand-in answered ENOENT to "extension issue" (5) and "extension consume" (6), so
 * sandbox_extension_issue_*() returned NULL everywhere. tccd, after granting kTCCServiceCamera/Microphone to Camera, issues the
 * client an extension for the service, got NULL and answered the request with an error (q10h7, Camera's own log: kTCCErrorDomain
 * Code=6 "server error: Failed to issue Sandbox Extension for kTCCServiceCamera", then AVFoundation -11852 "not authorized", no
 * capture input, black preview). SWEET has no sandbox, so an extension restricts and grants nothing: it only has to exist.
 *
 * libsystem_sandbox, iOS 16.7 (disassembly of the shared cache):
 *   issue    __sandbox_ms("Sandbox", 5, { u64 class (char *), u64 type (0 file, 1 mach, 2 iokit class, 3 generic, 4), u64 data (char *
 *            or NULL), u64 flags (0x10000: to pid, 0x30000: to audit token), u64 token (char *, 2048 bytes), u64 pid, u64 pidversion })
 *            the kernel writes the token, a C string; every sandbox_extension_issue_*() comes here (_sandbox_extension_issue).
 *   consume  __sandbox_ms("Sandbox", 6, { u64 token (char *), u64 strlen + 1, u64 handle (int64_t *) }); the function returns
 *            *handle, or -1 when the call fails.
 *   release  __sandbox_ms("Sandbox", 7, { u64 handle, 0, 0 }): already answered 0 by the default below.
 * The token is laid out like a real one (hex MAC field, then ';' separated fields, class, type, data) but the first field is not a
 * MAC: "5357454554" ("SWEET") and a serial. Consume accepts any readable token and returns a new positive handle.
 */
static uint64_t sweet_sandbox_ext_serial;
static uint64_t sweet_sandbox_ext_logged[32];           /* target pid and class, of the lines printed */
static unsigned sweet_sandbox_ext_lognext, sweet_sandbox_ext_lines;

static int
sweet_sandbox_extension_issue(struct proc *p, user_addr_t arg)
{
	uint64_t a[7], key = 0;
	char cls[128], data[256], tok[512], caller[32], target[32];
	size_t done = 0;
	int error;

	if (arg == USER_ADDR_NULL || !proc_is64bit(p)) {
		return EINVAL;
	}
	if ((error = copyin(arg, a, sizeof(a))) != 0) {
		return error;
	}
	if (a[0] == 0 || a[4] == 0) {
		return EINVAL;
	}
	error = copyinstr((user_addr_t)a[0], cls, sizeof(cls), &done);
	if (error != 0 && error != ENAMETOOLONG) {
		return error;
	}
	cls[sizeof(cls) - 1] = '\0';
	data[0] = '\0';
	if (a[2] != 0) {
		error = copyinstr((user_addr_t)a[2], data, sizeof(data), &done);
		if (error != 0 && error != ENAMETOOLONG) {
			return error;
		}
		data[sizeof(data) - 1] = '\0';
	}
	uint64_t serial = __sync_add_and_fetch(&sweet_sandbox_ext_serial, 1);
	snprintf(tok, sizeof(tok), "5357454554%054llx;00;00000000;00000000;00000000;%016llx;%s;%02x;%s",
	    (unsigned long long)serial, (unsigned long long)strlen(cls), cls, (unsigned)(a[1] & 0xff), data);
	if ((error = copyout(tok, (user_addr_t)a[4], strlen(tok) + 1)) != 0) {
		return error;
	}

	/* one line for the first issue of each class to each target process */
	int tpid = ((a[3] & 0x30000) != 0 && a[5] != 0 && a[5] <= (uint64_t)INT32_MAX) ? (int)a[5] : 0;
	for (const char *c = cls; *c; c++) {
		key = key * 131 + (unsigned char)*c;
	}
	key = (key << 24) ^ (uint64_t)(unsigned)tpid ^ (1ULL << 63);
	for (unsigned i = 0; i < 32; i++) {
		if (sweet_sandbox_ext_logged[i] == key) {
			return 0;
		}
	}
	if (sweet_sandbox_ext_lines < 400) {
		sweet_sandbox_ext_lines++;
		sweet_sandbox_ext_logged[sweet_sandbox_ext_lognext++ & 31] = key;
		caller[0] = '\0';
		proc_name(proc_pid(p), caller, sizeof(caller));
		strlcpy(target, tpid ? "?" : "(no process)", sizeof(target));
		if (tpid) {
			proc_name(tpid, target, sizeof(target));
		}
		printf("SWEET Sandbox: extension issued to %s[%d] by %s class %s type %u%s%.80s\n", target, tpid, caller, cls,
		    (unsigned)(a[1] & 0xff), data[0] ? " data " : "", data);
	}
	return 0;
}

static int
sweet_sandbox_extension_consume(struct proc *p, user_addr_t arg)
{
	uint64_t a[3];
	char first;
	int error;

	if (arg == USER_ADDR_NULL || !proc_is64bit(p)) {
		return EINVAL;
	}
	if ((error = copyin(arg, a, sizeof(a))) != 0) {
		return error;
	}
	if (a[0] == 0 || a[2] == 0) {
		return EINVAL;
	}
	if ((error = copyin((user_addr_t)a[0], &first, 1)) != 0) {
		return error;
	}
	int64_t handle = (int64_t)__sync_add_and_fetch(&sweet_sandbox_ext_serial, 1);
	return copyout(&handle, (user_addr_t)a[2], sizeof(handle));
}

/*
 * 0730 (jbts-20261115): "container path" (op 4) for app extensions.
 * libsystem_sandbox sandbox_container_path_for_pid(pid, buf, len) = __mac_syscall("Sandbox", 4,
 * { int64 pid (0 = self); u64 audit token pointer (0); u64 buf; u64 buflen; u64 0 }) [iOS 16.7 disasm].  PlugInKit checks it in
 * EVERY extension process at start (func_1aaf39c64: error or strlen(path) <= 1 -> NSInternalInconsistencyException "plugin has
 * invalid sandbox or container: error %d path %s" -> abort).  The stand-in answered ENOENT, so every .appex view service died at
 * once: com.apple.DocumentManager.Service (UIDocumentPickerViewController) -> "Couldn't communicate with a helper application"
 * in TrollStore "Install IPA File" (phone, ts1 capture: sig 6, bt objc_exception_throw <- PlugInKit).
 * Answer (self only, no audit token) with the process's own HOME (xpcproxy/RunningBoard set it to the data container), read from
 * its exec strings (the env block below user_stack, as KERN_PROCARGS2), else "/private/var/mobile".
 *   kern.n2sbcont  0 = ENOENT for everyone (before 0730); 1 = processes whose executable is inside a ".appex/" (default);
 *                  2 = also any process whose HOME is under /var/mobile/Containers/.  Boot-arg n2sbcont=N sets the start value.
 * Log: "NORD2 SBCONT: <name>[pid] container -> <path> (<why>)", first 32 answers.
 */
#include <sys/sysctl.h>
#include <sys/proc_internal.h>
#include <kern/kalloc.h>
#define SWEET_SBCONT_MIN(a, b) ((a) < (b) ? (a) : (b))
static unsigned int sweet_sbcont_mode = 1;
static int sweet_sbcont_bootarg_done;
static unsigned int sweet_sbcont_logged;
SYSCTL_UINT(_kern, OID_AUTO, n2sbcont, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_sbcont_mode, 0,
    "0730: Sandbox container path for 0 none, 1 .appex processes, 2 + containerized HOME");

/* Pure helper (host-tested in jbts-20261115/piece-0730/test): pick the answer from the executable path and the exec-string block. */
int sweet_sbcont_pick(unsigned int mode, const char *exe, const char *blk, size_t blen, char *out, size_t outlen, const char **why);
int
sweet_sbcont_pick(unsigned int mode, const char *exe, const char *blk, size_t blen, char *out, size_t outlen, const char **why)
{
	const char *home = NULL;
	size_t hlen = 0, i = 0;
	int appex = 0, contained = 0;

	if (mode == 0 || outlen < 2) {
		return ENOENT;
	}
	for (const char *s = exe; s != NULL && *s; s++) {
		if (strncmp(s, ".appex/", 7) == 0) {
			appex = 1;
			break;
		}
	}
	while (blk != NULL && i < blen) {               /* NUL-separated strings; the last HOME= wins (env comes after argv) */
		size_t n = strnlen(blk + i, blen - i);
		if (n > 5 && strncmp(blk + i, "HOME=", 5) == 0 && blk[i + 5] == '/') {
			home = blk + i + 5;
			hlen = n - 5;
		}
		i += n + 1;
	}
	if (home != NULL && (strncmp(home, "/private/var/mobile/Containers/", 31) == 0 ||
	    strncmp(home, "/var/mobile/Containers/", 23) == 0)) {
		contained = 1;
	}
	if (!(appex || (mode >= 2 && contained))) {
		return ENOENT;
	}
	if (home != NULL && hlen > 1) {
		const char *pre = strncmp(home, "/var/", 5) == 0 ? "/private" : "";
		size_t plen = strlen(pre);
		if (plen + hlen + 1 > outlen) {
			return ERANGE;
		}
		memcpy(out, pre, plen);
		memcpy(out + plen, home, hlen);
		out[plen + hlen] = '\0';
		*why = contained ? "HOME container" : "HOME";
		return 0;
	}
	if (outlen < sizeof("/private/var/mobile")) {
		return ERANGE;
	}
	strlcpy(out, "/private/var/mobile", outlen);
	*why = "no HOME, mobile home";
	return 0;
}

static int
sweet_sandbox_container_path(struct proc *p, user_addr_t arg)
{
	struct {
		int64_t  pid;
		uint64_t token;
		uint64_t buf;
		uint64_t len;
		uint64_t flags;
	} a;
	char exe[MAXPATHLEN], ans[MAXPATHLEN];
	const char *why = "";
	int elen = sizeof(exe), error;
	vnode_t vp;
	char *blk = NULL;
	size_t blen = 0;

	if (!sweet_sbcont_bootarg_done) {
		unsigned int v;
		if (PE_parse_boot_argn("n2sbcont", &v, sizeof(v))) {
			sweet_sbcont_mode = v;
		}
		sweet_sbcont_bootarg_done = 1;
	}
	if (sweet_sbcont_mode == 0 || arg == USER_ADDR_NULL || !proc_is64bit(p)) {
		return ENOENT;
	}
	if (copyin(arg, &a, sizeof(a)) != 0) {
		return EFAULT;
	}
	if ((a.pid != 0 && a.pid != proc_pid(p)) || a.token != 0 || a.buf == 0 || a.len < 2) {
		return ENOENT;                          /* only "my own container" is answered */
	}
	exe[0] = '\0';
	vp = proc_getexecutablevnode(p);
	if (vp != NULLVP) {
		if (vn_getpath(vp, exe, &elen) != 0) {
			exe[0] = '\0';
		}
		vnode_put(vp);
	}
	if (p->p_argslen > 0 && p->user_stack != 0) {
		blen = SWEET_SBCONT_MIN((size_t)p->p_argslen, (size_t)32768);        /* env strings are at the end of the block */
		blk = kalloc_data(blen, Z_WAITOK);
		if (blk != NULL && copyin(p->user_stack - blen, blk, blen) != 0) {
			kfree_data(blk, blen);
			blk = NULL;
		}
	}
	error = sweet_sbcont_pick(sweet_sbcont_mode, exe, blk, blk != NULL ? blen : 0, ans, SWEET_SBCONT_MIN(sizeof(ans), (size_t)a.len), &why);
	if (blk != NULL) {
		kfree_data(blk, blen);
	}
	if (error == 0) {
		error = copyout(ans, (user_addr_t)a.buf, strlen(ans) + 1);
	}
	if (error == 0 && sweet_sbcont_logged < 32) {
		char name[32];
		sweet_sbcont_logged++;
		proc_name(proc_pid(p), name, sizeof(name));
		printf("NORD2 SBCONT: %s[%d] container -> %s (%s)\n", name, proc_pid(p), ans, why);
	}
	return error;
}

/*
 * 0731 (jbts-20261115): diagnostic log of the Sandbox / AMFI policy calls and privilege denials of the file-provider stack
 * ('On My iPhone' missing: fileproviderd never launches com.apple.FileProvider.LocalStorage). For processes whose name holds one of
 * fileprovid, LocalStorage, Files, DocumentManag, pkd, extensionkit, TrollStore, launchd, every call is logged with its result:
 *   "SWEET SBLOG: <proc>[pid] Sandbox op N -> rc"  (op 2 = sandbox_check: + subject pid/name, operation, filter type, argument)
 *   "SWEET SBLOG: <proc>[pid] AMFI op N -> rc"     "SWEET SBLOG: <proc>[pid] privilege N denied"
 * kern.n2sblog: 0 off, 1 on (default); kern.n2sblog_left: lines left (default 800; write a number to re-arm). launchd's op 2 lines
 * are kept only when the subject is one of the watched names (it checks mach-lookups for everybody). Read-only, never changes a result.
 */
static unsigned int sweet_sblog_on = 1;
static int sweet_sblog_left = 800;
SYSCTL_UINT(_kern, OID_AUTO, n2sblog, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_sblog_on, 0, "0731: Sandbox/AMFI call log for the file-provider stack");
SYSCTL_INT(_kern, OID_AUTO, n2sblog_left, CTLFLAG_RW | CTLFLAG_LOCKED, &sweet_sblog_left, 0, "0731: log lines left");

static int
sweet_sblog_watched(const char *n)
{
	static const char *const w[] = { "fileprovid", "LocalStorage", "Files", "DocumentManag", "pkd", "extensionkit", "TrollStore", "FileProvider" };
	for (unsigned i = 0; i < sizeof(w) / sizeof(w[0]); i++) {
		if (sweet_name_has(n, w[i])) {
			return 1;
		}
	}
	return 0;
}

static int
sweet_sblog_take(void)
{
	if (!sweet_sblog_on || sweet_sblog_left <= 0) {
		return 0;
	}
	return __sync_sub_and_fetch(&sweet_sblog_left, 1) >= 0;
}

static void
sweet_sblog_sandbox(struct proc *p, int call, user_addr_t arg, int rc)
{
	char name[32], subject[32] = "", op[64] = "", what[160] = "";
	uint64_t a[7];
	size_t done = 0;
	int is_launchd;

	if (!sweet_sblog_on || sweet_sblog_left <= 0) {
		return;
	}
	proc_name(proc_pid(p), name, sizeof(name));
	is_launchd = strcmp(name, "launchd") == 0;
	if (!is_launchd && !sweet_sblog_watched(name)) {
		return;
	}
	if (call == 2 && arg != USER_ADDR_NULL && proc_is64bit(p) && copyin(arg, a, sizeof(a)) == 0) {
		if (a[1] != 0 && a[1] <= (uint64_t)INT32_MAX) {
			proc_name((int)a[1], subject, sizeof(subject));
		}
		if (a[2] != 0 && copyinstr((user_addr_t)a[2], op, sizeof(op), &done) != 0) {
			strlcpy(op, "?", sizeof(op));
		}
		if ((a[3] == 1 || a[3] == 6 || a[3] == 7) && a[4] != 0 && copyinstr((user_addr_t)a[4], what, sizeof(what), &done) != 0) {
			strlcpy(what, "?", sizeof(what));
		}
		if (is_launchd && !sweet_sblog_watched(subject)) {
			return;
		}
		if (sweet_sblog_take()) {
			printf("SWEET SBLOG: %s[%d] Sandbox op 2 -> %d subject %s[%d] '%s' type %llu '%s'\n", name, proc_pid(p), rc, subject,
			    (int)a[1], op, (unsigned long long)a[3], what);
		}
		return;
	}
	if (is_launchd) {
		return;
	}
	if (sweet_sblog_take()) {
		printf("SWEET SBLOG: %s[%d] Sandbox op %d -> %d\n", name, proc_pid(p), call, rc);
	}
}

int sweet_sblog_sig(struct vnode *vp);
int
sweet_sblog_sig(struct vnode *vp)
{
	char path[256], name[32];
	int len = sizeof(path);

	if (!sweet_sblog_on || sweet_sblog_left <= 0 || vp == NULL || vn_getpath(vp, path, &len) != 0) {
		return 0;
	}
	if (!sweet_name_has(path, "FileProvider") && !sweet_name_has(path, "LocalStorage") && !sweet_name_has(path, "DocumentsApp") &&
	    !sweet_name_has(path, "DocumentManager") && !sweet_name_has(path, "/Files.app/")) {
		return 0;
	}
	if (sweet_sblog_take()) {
		proc_selfname(name, sizeof(name));
		printf("SWEET SBLOG: %s[%d] code signature %s\n", name, proc_selfpid(), path);
	}
	return 1;
}

static int
sweet_sandbox_syscall_inner(struct proc *p, int call, user_addr_t arg)
{
	if (call >= 0 && call < 64 && !(sweet_sandbox_seen & (1ULL << call))) {
		sweet_sandbox_seen |= 1ULL << call;
		char name[32];
		proc_name(proc_pid(p), name, sizeof(name));
		printf("SWEET Sandbox: %s op %d\n", name, call);
	}
	if (call == 2) {        /* sandbox_check* */
		sweet_trace_sandbox_check(p, arg);
		sweet_sandbox_self_check(p, arg);       /* 0277v */
	}
	switch (call) {
	case 5:         /* extension issue (0288) */
		return sweet_sandbox_extension_issue(p, arg);
	case 6:         /* extension consume (0288) */
		return sweet_sandbox_extension_consume(p, arg);
	case 4:         /* container path (audit token / pid): 0730 answers app extensions */
		return sweet_sandbox_container_path(p, arg);
	case 0x43:      /* container expected */
		return ENOENT;
	default:
		return 0;
	}
}

static int
sweet_sandbox_syscall(struct proc *p, int call, user_addr_t arg)
{
	int rc = sweet_sandbox_syscall_inner(p, call, arg);
	sweet_sblog_sandbox(p, call, arg, rc);         /* 0731 */
	return rc;
}

/*
 * Policy "AMFI": dyld asks the kernel what a process may do before it expands
 * @rpath, @executable_path and @loader_path, via __mac_syscall("AMFI", 90,
 * { uint64_t input_flags; uint64_t *output_flags; }). Without a policy of
 * that name the call fails, dyld takes no permission and every app that links
 * its own frameworks dies with "security policy does not allow @ path
 * expansion". SWEET has no restricted processes: allow @ paths and the
 * classic fallback paths. Other operations keep failing as before.
 */
#define SWEET_AMFI_OP_DYLD_POLICY       90
#define SWEET_AMFI_DYLD_ALLOW_AT_PATH   (1ULL << 0)
#define SWEET_AMFI_DYLD_ALLOW_FALLBACK  (1ULL << 3)
/*
 * NORD2 applog (0287). libsystem_trace (iOS 16.7, _os_trace_allow_development_env_vars, once per
 * process) asks this same operation and honours OS_ACTIVITY_DT_MODE (os_log mirrored on standard
 * error, which 0285 puts in the environment of the programs applog_name names and 0285/0286 bring to
 * the kernel log) only when bit 8 of the answer is set. dyld of 16.7 reads bits 0..7 only
 * (ProcessConfig::Security masks the answer with 0xff), so bit 8 changes nothing there. It is set
 * only for the programs applog_name names; everybody else gets the same answer as before, and the
 * bits about library insertion and path variables (1, 5, 6) stay clear for all.
 */
#define SWEET_AMFI_DYLD_ALLOW_DEV_VARS  (1ULL << 8)
extern bool exec_applog_name_match(const char *name, size_t namelen);   /* bsd/kern/kern_exec.c (0285) */

static uint64_t sweet_amfi_seen;

static int
sweet_amfi_policy_syscall(struct proc *p, int call, user_addr_t arg)
{
	char name[32];
	{                                               /* 0731 */
		char wn[32];
		proc_name(proc_pid(p), wn, sizeof(wn));
		if (call != SWEET_AMFI_OP_DYLD_POLICY && sweet_sblog_watched(wn) && sweet_sblog_take()) {
			printf("SWEET SBLOG: %s[%d] AMFI op %d -> %d\n", wn, proc_pid(p), call, ENOPOLICY);
		}
	}
	if (call >= 0 && call < 128 && call != SWEET_AMFI_OP_DYLD_POLICY &&
	    !(sweet_amfi_seen & (1ULL << (call & 63)))) {
		sweet_amfi_seen |= 1ULL << (call & 63);
		proc_name(proc_pid(p), name, sizeof(name));
		printf("SWEET AMFI: %s op %d (unsupported)\n", name, call);
	}
	if (call != SWEET_AMFI_OP_DYLD_POLICY) {
		return ENOPOLICY;
	}
	struct {
		uint64_t   input_flags;
		user_addr_t output;
	} a;
	int error = copyin(arg, &a, sizeof(a));
	if (error != 0) {
		return error;
	}
	uint64_t flags = SWEET_AMFI_DYLD_ALLOW_AT_PATH | SWEET_AMFI_DYLD_ALLOW_FALLBACK;
	name[0] = 0;
	proc_name(proc_pid(p), name, sizeof(name));
	if (name[0] != 0 && exec_applog_name_match(name, strlen(name))) {
		static int applog_last_pid;     /* dyld and libsystem_trace both ask: one line per process */
		int pid = proc_pid(p);

		flags |= SWEET_AMFI_DYLD_ALLOW_DEV_VARS;
		if (applog_last_pid != pid) {
			applog_last_pid = pid;
			printf("NORD2 APPLOG: AMFI dev-env-vars enabled for %s pid %d\n", name, pid);
		}
	}
	return copyout(&flags, a.output, sizeof(flags));
}

static const struct mac_policy_ops sweet_amfi_mac_ops = {
	.mpo_vnode_check_signature = sweet_amfi_vnode_check_signature,
	.mpo_policy_syscall = sweet_amfi_policy_syscall,
};

/*
 * Privileges for non-root processes come from MAC policies (priv_check_cred
 * -> mac_priv_grant); on iOS Sandbox/AMFI grant them. Without a grant UIKit
 * apps die in CoreAnimation setup: work_interval_create(CA_CLIENT) needs
 * PRIV_WORK_INTERVAL ("Failed to create work interval", SpringBoard).
 * Grant that one; log the first denial of each other privilege per process.
 */
#define SWEET_PRIV_LOG_MAX      64
static struct {
	int  priv;
	char name[20];
} sweet_priv_logged[SWEET_PRIV_LOG_MAX];
static int sweet_priv_logged_count;

static int
sweet_sandbox_priv_grant(kauth_cred_t cred __unused, int priv)
{
	if (priv == PRIV_WORK_INTERVAL) {
		return 0;
	}
	char name[20];
	proc_selfname(name, sizeof(name));
	if (sweet_sblog_watched(name) && sweet_sblog_take()) {      /* 0731 */
		printf("SWEET SBLOG: %s privilege %d denied\n", name, priv);
	}
	for (int i = 0; i < sweet_priv_logged_count; i++) {
		if (sweet_priv_logged[i].priv == priv &&
		    strncmp(sweet_priv_logged[i].name, name, sizeof(name)) == 0) {
			return EPERM;
		}
	}
	if (sweet_priv_logged_count < SWEET_PRIV_LOG_MAX) {
		sweet_priv_logged[sweet_priv_logged_count].priv = priv;
		strlcpy(sweet_priv_logged[sweet_priv_logged_count].name, name, sizeof(name));
		sweet_priv_logged_count++;
		printf("SWEET Sandbox: %s privilege %d denied\n", name, priv);
	}
	return EPERM;
}

/*
 * 0277c NORD2 BB: what the baseband stack does is invisible from the kernel (ABM logs through os_log). These hooks of the permissive
 * Sandbox policy report the path lookups, existing-file stats, IOKit property reads and IOKit opens of CommCenter and abm-helper to the
 * bridge (Nord2Baseband.cpp), which deduplicates and logs them ("NORD2 BB: trace ..."). Other processes cost one name compare. They
 * never deny anything, except the one lookup the bridge's path_gate fails (0277e, CommCenter's stale /tmp/ct.shutdown).
 * 0277e adds sysctl names and the Sandbox checks (launchd's mach-lookup checks) whose subject is CommCenter or abm-helper.
 */
#include "nord2_bb_kpi.h"
static int
sweet_bb_traced(void)
{
	char name[32];

	if (nord2_bb_hooks == NULL || nord2_bb_hooks->trace_path == NULL) {
		return 0;
	}
	proc_selfname(name, sizeof(name));
	return !strcmp(name, "CommCenter") || !strcmp(name, "abm-helper");
}

static int
sweet_trace_lookup(__unused kauth_cred_t cred, __unused struct vnode *dvp, __unused struct label *dlabel, const char *path,
    __unused size_t pathlen)
{
	/* 0277d: namei's buffer is NUL terminated; pathlen is not the whole path at this point (0277c logged "pri", "System/Lib") */
	if (path != NULL && sweet_bb_traced()) {
		size_t len = strnlen(path, 1024);

		nord2_bb_hooks->trace_path("lookup", path, len);
		/* 0277e: the bridge may fail one lookup (CommCenter's first-launch check of a stale /tmp/ct.shutdown) */
		if (nord2_bb_hooks->path_gate != NULL) {
			return nord2_bb_hooks->path_gate(path, len);
		}
	}
	return 0;
}

/* 0277e: sysctl names read (or written) by CommCenter and abm-helper */
static int
sweet_trace_sysctl(__unused kauth_cred_t cred, const char *namestring, __unused int *name, __unused size_t namelen,
    __unused user_addr_t old, __unused size_t oldlen, user_addr_t newvalue, __unused size_t newlen)
{
	if (namestring != NULL && sweet_bb_traced()) {
		nord2_bb_hooks->trace_path(newvalue != USER_ADDR_NULL ? "sysctl-set" : "sysctl", namestring, strnlen(namestring, 128));
	}
	return 0;
}

/*
 * 0277e: sandbox_check(), sandbox_check_by_audit_token() (libsystem_sandbox, iOS 16) pass __mac_syscall("Sandbox", 2) a pointer to
 * { u64 result *, u64 pid, u64 operation (char *), u64 filter type, u64 filter argument, u64 flags, u64 pid version }; libsystem_sandbox
 * maps SANDBOX_FILTER_PATH to type 1, GLOBAL_NAME to 6 and LOCAL_NAME to 7, whose argument is a string. launchd checks every mach-lookup
 * of every process this way (without a "Sandbox" policy it answered "error = 159: Sandbox restriction"), so the checks whose subject
 * is CommCenter or abm-helper show which mach services they look up. Trace only: the answer stays "allowed" (nothing is written to
 * the result).
 */
static void
sweet_trace_sandbox_check(struct proc *p, user_addr_t arg)
{
	uint64_t a[7];
	char subject[32], op[48], what[160];
	size_t done = 0;

	if (nord2_bb_hooks == NULL || nord2_bb_hooks->trace_check == NULL || arg == USER_ADDR_NULL || !proc_is64bit(p) ||
	    copyin(arg, a, sizeof(a)) != 0 || a[1] == 0 || a[1] > (uint64_t)INT32_MAX || a[2] == 0) {
		return;
	}
	subject[0] = '\0';
	proc_name((int)a[1], subject, sizeof(subject));
	if (strcmp(subject, "CommCenter") != 0 && strcmp(subject, "abm-helper") != 0) {
		return;
	}
	if (copyinstr((user_addr_t)a[2], op, sizeof(op), &done) != 0) {
		return;
	}
	what[0] = '\0';
	if ((a[3] == 1 || a[3] == 6 || a[3] == 7) && a[4] != 0 &&
	    copyinstr((user_addr_t)a[4], what, sizeof(what), &done) != 0) {
		strlcpy(what, "?", sizeof(what));
	}
	nord2_bb_hooks->trace_check(subject, op, what);
}

static int
sweet_trace_stat(__unused kauth_cred_t active_cred, __unused kauth_cred_t file_cred, struct vnode *vp, __unused struct label *label)
{
	char path[512];
	int len = sizeof(path);

	if (vp != NULL && sweet_bb_traced() && vn_getpath(vp, path, &len) == 0) {
		nord2_bb_hooks->trace_path("stat", path, strnlen(path, sizeof(path)));
	}
	return 0;
}

static int
sweet_trace_ioprop(__unused kauth_cred_t cred, io_object_t entry, const char *name)
{
	if (entry != NULL && name != NULL && sweet_bb_traced() && nord2_bb_hooks->trace_ioprop != NULL) {
		nord2_bb_hooks->trace_ioprop(entry, name);
	}
	return 0;
}

static int
sweet_trace_ioopen(__unused kauth_cred_t cred, io_object_t service, unsigned int type)
{
	if (service != NULL && sweet_bb_traced() && nord2_bb_hooks->trace_ioopen != NULL) {
		nord2_bb_hooks->trace_ioopen(service, type);
	}
	return 0;
}

/*
 * 0277y: who removes /var/mobile/Library/Application Support/{com.apple.remotemanagementd,com.apple.ProtectedCloudStorage,
 * com.apple.ap.adprivacyd} at boot (q10cn: ScreenTimeAgent, ProtectedCloudKeySyncing and adprivacyd all saw "vnode unlinked while in
 * use" within 2 ms at 22.22 s, the two first directories were gone after the boot). Unlinks, rmdirs and renames of those names and of
 * the store files in them are logged with the process doing it ("SWEET FS: <proc>[pid] unlink|rename <path> [-> <target>]"), at most
 * 64 lines a boot. A cheap name compare first: everything else costs nothing. Never denies anything.
 */
static int
sweet_fs_watched(const struct componentname *cnp)
{
	static const char *const names[] = {
		"com.apple.remotemanagementd", "com.apple.ProtectedCloudStorage", "com.apple.ap.adprivacyd", "Application Support",
		"RMAdminStore", "FeatureModel.sqlite", "PCSAnalytics.db", "KeysSyncingVersion3",
	};
	size_t i, n;

	if (cnp == NULL || cnp->cn_nameptr == NULL || cnp->cn_namelen <= 0) {
		return 0;
	}
	for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		n = strlen(names[i]);
		if ((size_t)cnp->cn_namelen >= n && strncmp(cnp->cn_nameptr, names[i], n) == 0) {
			return 1;
		}
	}
	return 0;
}

static unsigned sweet_fs_lines;

static void
sweet_fs_log(const char *op, struct vnode *vp, struct vnode *tdvp, const struct componentname *tcnp)
{
	char name[32], path[256], to[256];
	int len = sizeof(path), tlen = sizeof(to);

	if (sweet_fs_lines >= 64) {
		return;
	}
	sweet_fs_lines++;
	proc_selfname(name, sizeof(name));
	if (vp == NULL || vn_getpath(vp, path, &len) != 0) {
		strlcpy(path, "?", sizeof(path));
	}
	to[0] = 0;
	if (tdvp != NULL && tcnp != NULL && vn_getpath(tdvp, to, &tlen) == 0) {
		size_t l = strnlen(to, sizeof(to));
		snprintf(to + l, sizeof(to) - l, "/%.*s", (int)tcnp->cn_namelen, tcnp->cn_nameptr);
	}
	printf("SWEET FS: %s[%d] %s%s %s%s%s\n", name, proc_selfpid(), op, vp != NULL && vnode_isdir(vp) ? " (dir)" : "", path,
	    to[0] ? " -> " : "", to);
}

static int
sweet_fs_unlink(__unused kauth_cred_t cred, __unused struct vnode *dvp, __unused struct label *dlabel, struct vnode *vp,
    __unused struct label *label, struct componentname *cnp)
{
	if (sweet_fs_watched(cnp)) {
		sweet_fs_log("unlink", vp, NULL, NULL);
	}
	return 0;
}

static int
sweet_fs_rename(__unused kauth_cred_t cred, __unused struct vnode *dvp, __unused struct label *dlabel, struct vnode *vp,
    __unused struct label *label, struct componentname *cnp, struct vnode *tdvp, __unused struct label *tdlabel,
    __unused struct vnode *tvp, __unused struct label *tlabel, struct componentname *tcnp)
{
	if (sweet_fs_watched(cnp) || sweet_fs_watched(tcnp)) {
		sweet_fs_log("rename", vp, tdvp, tcnp);
	}
	return 0;
}

static const struct mac_policy_ops sweet_sandbox_mac_ops = {
	.mpo_policy_syscall = sweet_sandbox_syscall,
	.mpo_priv_grant = sweet_sandbox_priv_grant,
	.mpo_vnode_check_lookup_preflight = sweet_trace_lookup,      /* 0277c */
	.mpo_vnode_check_stat = sweet_trace_stat,
	.mpo_iokit_check_get_property = sweet_trace_ioprop,
	.mpo_iokit_check_open_service = sweet_trace_ioopen,
	.mpo_system_check_sysctlbyname = sweet_trace_sysctl,         /* 0277e */
	.mpo_vnode_check_unlink = sweet_fs_unlink,                   /* 0277y */
	.mpo_vnode_check_rename = sweet_fs_rename,
};

static struct mac_policy_conf sweet_sandbox_mac_conf = {
	.mpc_name = "Sandbox",
	.mpc_fullname = "SWEET permissive sandbox stand-in",
	.mpc_ops = &sweet_sandbox_mac_ops,
};

static mac_policy_handle_t sweet_sandbox_mac_handle;

static struct mac_policy_conf sweet_amfi_mac_conf = {
	.mpc_name = "AMFI",
	.mpc_fullname = "SWEET built-in Mobile File Integrity",
	.mpc_ops = &sweet_amfi_mac_ops,
};

static mac_policy_handle_t sweet_amfi_mac_handle;


/*
 * p1: SWEET_AUTOTEST 1 = the c8x automated test exactly (RTC fastboot bit armed at boot, one 300 s modem session, then a self reset
 * into fastboot); 0 = persistent daily build: no fastboot bit, no self reset, the modem service runs forever in its own kernel thread.
 * /dev/n2at (userspace AT) exists in both.
 */
#ifndef SWEET_PERSIST_TEST
#define SWEET_PERSIST_TEST 0          /* 0277 daily build (bb1d): no timed reset; the bb1 test builds use 1 (7 min, then fastboot) */
#endif
#ifndef SWEET_ARM_FB
#define SWEET_ARM_FB 1                /* keep the RTC fastboot bit armed so any crash lands in fastboot (remote maintenance) */
#endif
#ifndef SWEET_AUTOTEST
#define SWEET_AUTOTEST 0
#endif

/*
 * 0202 read-only modem probe (MT6893 MD1).  LK loads md1img and leaves the modem unstarted; this reports what the
 * firmware/ATF/SPM look like at iOS boot.  It reads INFRA_AO (always on) and DRAM only; it never touches a clock-gated
 * block (CCIF/DPMAIF), never calls MD_KERNEL_BOOT_UP and never changes any state.  Sources: Linux eccci
 * mt6885/md_sys1_platform.c, modem_secure_base.h, mtk_sip_svc.h (MTK_SIP_KERNEL_CCCI_CONTROL = 0xC2000505).
 */
extern vm_offset_t ml_io_map(vm_offset_t phys_addr, vm_size_t size);

static void
sweet_modem_smc(uint64_t sub, uint64_t arg, uint64_t out[4])
{
	register uint64_t x0 __asm__("x0") = 0xC2000505ULL;
	register uint64_t x1 __asm__("x1") = sub;
	register uint64_t x2 __asm__("x2") = arg;
	register uint64_t x3 __asm__("x3") = 0;
	register uint64_t x4 __asm__("x4") = 0;
	register uint64_t x5 __asm__("x5") = 0;
	register uint64_t x6 __asm__("x6") = 0;
	register uint64_t x7 __asm__("x7") = 0;
	__asm__ volatile ("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3),
	    "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7) : :
	    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "cc", "memory");
	out[0] = x0; out[1] = x1; out[2] = x2; out[3] = x3;
}

static void
sweet_modem_dump(const char *tag, uintptr_t pa, unsigned words)
{
	volatile uint32_t *v = (volatile uint32_t *)ml_io_map(pa, 0x1000);
	if (v == NULL) {
		printf("NORD2 MODEM: %s %#lx map failed\n", tag, (unsigned long)pa);
		return;
	}
	printf("NORD2 MODEM: %s @%#lx:", tag, (unsigned long)pa);
	for (unsigned i = 0; i < words; i++) {
		printf(" %08x", v[i]);
	}
	printf("\n");
}

static void
sweet_modem_probe(void)
{
	static const struct { const char *name; uint64_t arg; } q[] = {
		{ "CHECK_DONE", 3 }, { "CHECK_FLAG", 2 }, { "BOOT_STATUS", 4 },
	};
	uint64_t r[4];
	volatile uint32_t *infra = (volatile uint32_t *)ml_io_map(0x10000000UL, 0x1000);

	if (infra != NULL) {
		printf("NORD2 MODEM: infra_ao MD_SRCCLKENA(0xF0C)=%08x\n", infra[0xF0C / 4]);
	}
	for (unsigned i = 0; i < sizeof(q) / sizeof(q[0]); i++) {
		sweet_modem_smc(6 /* MD_POWER_CONFIG */, q[i].arg, r);
		printf("NORD2 MODEM: smc POWER_CONFIG %s -> %llx %llx %llx %llx\n", q[i].name, r[0], r[1], r[2], r[3]);
	}
	sweet_modem_dump("smem_a", 0x8e000000UL, 16);
	sweet_modem_dump("md_img", 0x8a000000UL, 8);
}


/*
 * 0203: MD1 MTCMOS power-on and CCIF access (stage 2 of the modem bring-up), Linux clk-mt6885-pg.c spm_mtcmos_ctrl_md1()
 * and clk-mt6885.c gates.  SPM 0x10006000, infracfg_ao 0x10001000.  The sequence only runs when SPM reports MD1 off; every
 * wait is bounded.  CCIF (0x10209000) is read only after the domain is on and the CCIF AP/MD clock gates are released.
 * The modem is NOT started: no MD_KERNEL_BOOT_UP.
 */
extern void delay_for_interval(uint32_t interval, uint32_t scale_factor);

static void
sweet_modem_wr(volatile uint32_t *base, uint32_t off, uint32_t v)
{
	base[off / 4] = v;
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
sweet_modem_power(void)
{
	volatile uint32_t *spm = (volatile uint32_t *)ml_io_map(0x10006000UL, 0x1000);
	volatile uint32_t *ic = (volatile uint32_t *)ml_io_map(0x10001000UL, 0x1000);
	unsigned n;

	if (spm == NULL || ic == NULL) {
		printf("NORD2 MODEM: spm/infracfg map failed\n");
		return;
	}
	printf("NORD2 MODEM: spm PWR_STATUS=%08x 2ND=%08x MD1_PWR_CON=%08x EXT_BUCK_ISO=%08x\n",
	    spm[0x16C / 4], spm[0x170 / 4], spm[0x300 / 4], spm[0x398 / 4]);
	printf("NORD2 MODEM: infracfg CG1_STA=%08x (ccif_ap b23, ccif_md b26) CG3_STA=%08x (dpmaif b26) PROT_STA1=%08x\n",
	    ic[0x94 / 4], ic[0xC8 / 4], ic[0x228 / 4]);

	if ((spm[0x16C / 4] & 1) == 0) {
		printf("NORD2 MODEM: MD1 domain off, powering on (Linux sequence)\n");
		sweet_modem_wr(spm, 0x000, (0xB16U << 16) | 1U);
		sweet_modem_wr(spm, 0x398, spm[0x398 / 4] & ~0x3U);
		sweet_modem_wr(spm, 0x300, spm[0x300 / 4] | 0x1U);         /* PWR_RST_B */
		sweet_modem_wr(spm, 0x300, spm[0x300 / 4] | 0x4U);         /* PWR_ON */
		for (n = 0; n < 20000 && (spm[0x16C / 4] & 1) == 0; n++) {
			delay_for_interval(10, NSEC_PER_USEC);
		}
		printf("NORD2 MODEM: MD1 PWR_STATUS after %u polls = %08x\n", n, spm[0x16C / 4]);
		if ((spm[0x16C / 4] & 1) == 0) {
			printf("NORD2 MODEM: MD1 power-on timed out, stopping\n");
			return;
		}
		sweet_modem_wr(ic, 0xB88, (1U << 10) | (1U << 21) | (1U << 29));   /* VDNR_CLR step2_0 */
		sweet_modem_wr(ic, 0xBBC, (1U << 6) | (1U << 7));                   /* SUB_INFRA_VDNR_CLR step2_1 */
		sweet_modem_wr(ic, 0x2A4, 1U << 7);                                 /* PROTECTEN_CLR step1_0 */
		printf("NORD2 MODEM: bus protect released, PROT_STA1=%08x\n", ic[0x228 / 4]);
	}
	/* ungate CCIF AP/MD (CG1 clr 0x8c, bits 23/26); gates are active-high */
	sweet_modem_wr(ic, 0x8C, (1U << 23) | (1U << 26));
	printf("NORD2 MODEM: CG1_STA after ungate=%08x\n", ic[0x94 / 4]);
	sweet_modem_dump("ccif0", 0x10209000UL, 8);
	sweet_modem_dump("ccif0_sram", 0x1020a000UL, 8);
}


/*
 * 0204: sample the modem image / shared-memory regions at several offsets, now and 30 s later, to learn whether LK left the
 * image in DRAM and whether anything (modem, ATF) clears it.  Read-only.
 */
#include <kern/thread_call.h>
#include <kern/clock.h>

static thread_call_t sweet_modem_late_call;
static bool sweet_modem_second;

static void
sweet_modem_sample(const char *when)
{
	static const uint32_t img_off[] = { 0x0, 0x40 };
	static const uint32_t smem_off[] = { 0x0, 0x40 };
	volatile uint32_t *img = (volatile uint32_t *)ml_io_map(0x8a000000UL, 0x1000);
	volatile uint32_t *sm = (volatile uint32_t *)ml_io_map(0x8e000000UL, 0x1000);

	if (img != NULL) {
		for (unsigned i = 0; i < sizeof(img_off) / sizeof(img_off[0]); i++) {
			printf("NORD2 MODEM: %s img+%x: %08x %08x\n", when, img_off[i], img[img_off[i] / 4], img[img_off[i] / 4 + 1]);
		}
	}
	if (sm != NULL) {
		for (unsigned i = 0; i < sizeof(smem_off) / sizeof(smem_off[0]); i++) {
			printf("NORD2 MODEM: %s smem+%x: %08x %08x\n", when, smem_off[i], sm[smem_off[i] / 4], sm[smem_off[i] / 4 + 1]);
		}
	}
}

static void
sweet_modem_late(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	uint64_t r[4];
	volatile uint32_t *spm = (volatile uint32_t *)ml_io_map(0x10006000UL, 0x1000);

	sweet_modem_sample("late");
	sweet_modem_smc(6, 4, r);
	printf("NORD2 MODEM: late BOOT_STATUS -> %llx %llx %llx\n", r[0], r[1], r[2]);
	if (spm != NULL) {
		printf("NORD2 MODEM: late PWR_STATUS=%08x\n", spm[0x16C / 4]);
	}
	sweet_modem_dump("late ccif0", 0x10209000UL, 16);
	if (!sweet_modem_second) {
		uint64_t dl;
		sweet_modem_second = true;
		clock_interval_to_deadline(60000, NSEC_PER_MSEC, &dl);
		thread_call_enter_delayed(sweet_modem_late_call, dl);
	}
}

static void
sweet_modem_late_start(void)
{
	sweet_modem_sample("early");
	sweet_modem_late_call = thread_call_allocate(sweet_modem_late, NULL);
	if (sweet_modem_late_call != NULL) {
		uint64_t dl;
		clock_interval_to_deadline(30000, NSEC_PER_MSEC, &dl);
		(void)dl;                                       /* 0245: late probe disabled (modem start runs at +20 s alone) */
	}
}


/*
 * 0206: start MD1 from XNU and watch CCIF (stage 3).  Linux order (modem_sys1.c md_cd_start, md_sys1_platform.c md_cd_power_on and
 * md_cd_let_md_go): CCIF SRAM reset, MD SRCCLKENA, SPM sleep regs, flight mode off, MTCMOS (already on), SMC MD_KERNEL_BOOT_UP.
 * Nothing answers the modem yet, so it is expected to stall at the CCIF handshake; the goal is to log what it sends first.
 * Runs once, 20 s after policy registration, from a thread call; polling is bounded to 25 s.
 */
static thread_call_t sweet_modem_start_call;

/*
 * 0246: automated test loop.  sweet_arm_fastboot() sets MT6359P RTC_PDN1 (0x5b4) bit13 (RTC_FAST_BOOT) and triggers RTC_WRTGR (0x5c2) through
 * PWRAP SWINF2 (0x10026000), so that ANY following reset (our own, the AP watchdog after a hang) lands in LK fastboot; LK clears the bit.
 * sweet_reset_now() resets through TOPRGU (0x10007000) like mtk_wdt_restart().  Verified from Linux: bit + reset -> fastboot in 9 s.
 */
#if SWEET_AUTOTEST || SWEET_PERSIST_TEST || SWEET_ARM_FB
static int
sweet_pw_wait(volatile uint32_t *pw, unsigned fsm)
{
	for (unsigned i = 0; i < 100000; i++) {
		if (((pw[0xca8 / 4] >> 1) & 7) == fsm) {
			return 0;
		}
	}
	return -1;
}

static int
sweet_pw_read(volatile uint32_t *pw, uint32_t addr, uint32_t *val)
{
	if (sweet_pw_wait(pw, 0)) {
		return -1;
	}
	pw[0xc80 / 4] = addr;
	if (sweet_pw_wait(pw, 6)) {
		return -2;
	}
	*val = pw[0xc94 / 4] & 0xffff;
	pw[0xca4 / 4] = 1;
	return 0;
}

static int
sweet_pw_write(volatile uint32_t *pw, uint32_t addr, uint32_t val)
{
	if (sweet_pw_wait(pw, 0)) {
		return -1;
	}
	pw[0xc84 / 4] = val;
	pw[0xc80 / 4] = (1U << 29) | addr;
	return sweet_pw_wait(pw, 0) ? -2 : 0;
}

static void
sweet_arm_fastboot(void)
{
	volatile uint32_t *pw = (volatile uint32_t *)ml_io_map(0x10026000UL, 0x1000);
	uint32_t v = 0, v2 = 0;
	int rc1, rc2 = -9, rc3 = -9, rc4 = -9;

	if (pw == NULL) {
		printf("NORD2 AUTOFB: pwrap map failed\n");
		return;
	}
	rc1 = sweet_pw_read(pw, 0x5b4, &v);
	if (rc1 == 0) {
		rc2 = sweet_pw_write(pw, 0x5b4, v | 0x2000);
		rc3 = sweet_pw_write(pw, 0x5c2, 1);
		for (unsigned i = 0; i < 2000; i++) {       /* rtc_busy_wait: RTC_BBPU (0x588) bit6 CBUSY */
			uint32_t b = 0;
			if (sweet_pw_read(pw, 0x588, &b) || !(b & 0x40)) {
				break;
			}
		}
		rc4 = sweet_pw_read(pw, 0x5b4, &v2);
	}
	printf("NORD2 AUTOFB: RTC_PDN1 %04x -> %04x (rc %d %d %d %d): next reset enters fastboot\n", v, v2, rc1, rc2, rc3, rc4);
}

static void __attribute__((unused))
sweet_reset_now(const char *why)
{
	volatile uint32_t *wdt = (volatile uint32_t *)ml_io_map(0x10007000UL, 0x1000);

	printf("NORD2 AUTOFB: self reset (%s)\n", why);
	delay_for_interval(8000, NSEC_PER_MSEC);         /* let the USB console drain */
	sweet_arm_fastboot();
	if (wdt == NULL) {
		return;
	}
	wdt[0] = (wdt[0] & ~(1U << 3)) | 0x22000000U;
	for (;;) {
		wdt[0x14 / 4] = 0x1209;
		delay_for_interval(5, NSEC_PER_MSEC);
	}
}
#endif /* SWEET_AUTOTEST || SWEET_PERSIST_TEST */

static void sweet_modem_start_inner(void);



/*
 * N2NET: DPMAIF register pages, mapped ONCE (I/O VA is scarce): AO 0x10014000 (UL +0, DL +0x400, MISC_AO/MD remap +0x800), PD 0x1022d000
 * (UL +0, DL +0x100, WDMA +0x300, AP_MISC +0x400), PD_MD_MISC 0x1022c000, infracfg_ao_mem 0x10002000.  Block order = the N2D_* indexes.
 */
static volatile uint32_t *sweet_n2d_blk[8];        /* AO_UL, AO_DL, AO_MD, PD_UL, PD_DL, PD_WDMA, PD_MISC, MD_MISC */
static volatile uint32_t *sweet_n2d_inframem;

static int
sweet_n2d_map(void)
{
	static int tried;

	if (!tried) {
		volatile uint32_t *ao = (volatile uint32_t *)ml_io_map(0x10014000UL, 0x1000);
		volatile uint32_t *pd = (volatile uint32_t *)ml_io_map(0x1022d000UL, 0x1000);

		tried = 1;
		sweet_n2d_inframem = (volatile uint32_t *)ml_io_map(0x10002000UL, 0x1000);
		sweet_n2d_blk[7] = (volatile uint32_t *)ml_io_map(0x1022c000UL, 0x1000);
		if (ao != NULL) {
			sweet_n2d_blk[0] = ao;
			sweet_n2d_blk[1] = ao + 0x400 / 4;
			sweet_n2d_blk[2] = ao + 0x800 / 4;
		}
		if (pd != NULL) {
			sweet_n2d_blk[3] = pd;
			sweet_n2d_blk[4] = pd + 0x100 / 4;
			sweet_n2d_blk[5] = pd + 0x300 / 4;
			sweet_n2d_blk[6] = pd + 0x400 / 4;
		}
	}
	return (sweet_n2d_blk[0] != NULL && sweet_n2d_blk[3] != NULL && sweet_n2d_blk[7] != NULL && sweet_n2d_inframem != NULL) ? 0 : -1;
}

/*
 * 0238: DPMAIF hardware bring-up exactly as Linux's dpmaif_start()/drv_dpmaif_common_hw_init() does it BEFORE the modem is released
 * (nord2-current-kernel eccci/hif/ccci_hif_dpmaif.c, dpmaif_drv.c; MT6297 layout from the stock DT: ao_ul 0x10014000, ao_dl +0x400,
 * ao_md_dl +0x800, pd_ul 0x1022d000 (dl +0x100, wdma +0x300, misc +0x400), infracfg_ao_mem 0x10002000).  Each step is logged and followed
 * by a 3 s pause so a hang identifies the register.  The MD remap values are the ones Linux logged ([remap]-A.. = 0x00e300e1 ..).
 */
static uint32_t sweet_dp_val[4];

static void
sweet_dpmaif_step(const char *what)
{
	/* 0239: runs inside the MD1 on->boot-up window, so no printf/delay here; values are printed afterwards */
	(void)what;
}

static void
sweet_dpmaif_init(void)
{
	volatile uint32_t *inframem, *ao, *pd, *aomd, *wdma;
	volatile uint32_t v;
	static const uint32_t remap[8] = { 0x00e300e1, 0x00e700e5, 0x00eb00e9, 0x00ef00ed, 0x00f300f1, 0x00f700f5, 0x00fb00f9, 0x00ff00fd };
	unsigned i;

	if (sweet_n2d_map() != 0) {                    /* N2NET: shared mappings (already made by sweet_net_prepare) */
		printf("NORD2 MODEM: dpmaif: mapping failed\n");
		return;
	}
	inframem = sweet_n2d_inframem;
	ao = sweet_n2d_blk[0];
	pd = sweet_n2d_blk[3];
	aomd = ao + 0x800 / 4;
	wdma = pd + 0x300 / 4;
	sweet_dpmaif_step("read infracfg_ao_mem+0");
	v = inframem[0];
	sweet_dp_val[0] = v;
	sweet_dpmaif_step("set infracfg_ao_mem+0 bit10 (clear DPMAIF bus protect)");
	inframem[0] = v | (1U << 10);
	__asm__ volatile ("dsb sy" ::: "memory");
	sweet_dp_val[1] = inframem[0];
	sweet_dpmaif_step("read PD_MISC CG_EN (0x1022d468)");
	v = pd[(0x400 + 0x68) / 4];
	sweet_dp_val[2] = v;
	sweet_dpmaif_step("write PD_MISC CG_EN = 0x7f");
	pd[(0x400 + 0x68) / 4] = 0x7f;
	(void)wdma;                                 /* 0240: WDMA tweak dropped (bisect) */
	(void)aomd; (void)remap; (void)i;          /* 0241: MD remap block dropped (bisect) */
	sweet_dpmaif_step("AO_UL L1TIMR0 + DL_RESERVE_RW power flag 0xff");
	/* 0240: AO_UL L1TIMR0 dropped (bisect) */
	pd[(0x100 + 0xA8) / 4] = 0xff;
	__asm__ volatile ("dsb sy" ::: "memory");
	sweet_dpmaif_step("done (dpmaif common hw init)");
}


extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);
/* 0237: after the MD raises D2H_EXCEPTION_INIT (RCHNUM bit 16) read, region by region with a log and 3 s before each, the areas where the modem
 * writes why it failed (Linux's mdee_dumper reads them at this point). */
static void
sweet_ee_peek(void)
{
	static const struct { uintptr_t pa; unsigned words; const char *name; } pk[] = {
		{ 0x8e110000UL, 24, "MDCCCI_DBG" }, { 0x8e110800UL, 512, "MDSS_DBG" }, { 0x8e11f000UL, 32, "MD_RT" },
	};
	unsigned i, j;
	for (i = 0; i < sizeof(pk) / sizeof(pk[0]); i++) {
		volatile uint32_t *w;
		printf("NORD2 MODEM: ee-peek-about %s @%#lx\n", pk[i].name, (unsigned long)pk[i].pa);
		delay_for_interval(3000, NSEC_PER_MSEC);
		w = (volatile uint32_t *)ml_io_map_wcomb(pk[i].pa & ~0xfffUL, 0x1000);
		if (w == NULL) {
			continue;
		}
		w += (pk[i].pa & 0xfff) / 4;
		for (j = 0; j < pk[i].words; j += 8) {
			printf("NORD2 MODEM: ee-peek %s +%03x: %08x %08x %08x %08x %08x %08x %08x %08x\n", pk[i].name, j * 4,
			    w[j], w[j + 1], w[j + 2], w[j + 3], w[j + 4], w[j + 5], w[j + 6], w[j + 7]);
		}
		delay_for_interval(500, NSEC_PER_MSEC);
	}
	printf("NORD2 MODEM: ee-peek done\n");
}


/*
 * 0216: CCIF ring queues (Linux hif/ccci_hif_ccif.c md_ccif_ring_buf_init + ccci_ringbuf.c).  16 queues in CCISM_MCU (0x8e139000), then the
 * 16 exception rings in CCISM_MCU_EXP (0x8e1ed400).  Per queue: u32 0xEE0000EE x2, {rx.read, rx.write, rx.length, tx.read, tx.write,
 * tx.length}, rx data (MD->AP), tx data (AP->MD), u32 0xFF0000FF x2.  Packets: {0xAABBAABB, len} + ccci_header(16) + payload, padded to 8,
 * + {0xCCDDEEFF, 0xCCDDEEFF}.  Shared memory is accessed with ml_io_map_wcomb: the Device-nGnRnE mapping of ml_io_map hangs on these ranges.
 */
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);

static const uint32_t sweet_rxq[16] = { 80 << 10, 80 << 10, 40 << 10, 80 << 10, 20 << 10, 20 << 10, 64 << 10, 0, 8 << 10, 0, 0, 0, 0, 0, 0, 0 };
static const uint32_t sweet_txq[16] = { 128 << 10, 40 << 10, 8 << 10, 40 << 10, 20 << 10, 20 << 10, 64 << 10, 0, 8 << 10, 0, 0, 0, 0, 0, 0, 0 };
/* exception ring sizes (rx = tx): 12,32,8,0,0,0,8 KiB - not created, see sweet_rings_init */

static volatile uint32_t *sweet_ring[16];     /* ring i control block (after the 8-byte header), word pointer */
static uint32_t sweet_ring_pa[16];

static void
sweet_ring_create(volatile uint32_t *b, uint32_t rx, uint32_t tx)
{
	uint32_t buflen = 40 + rx + tx, i;

	for (i = 0; i < 8; i++) {          /* 0254: only header + control block (bulk memset of the data hangs the AP while the LK modem runs) */
		b[i] = 0;
	}
	b[0] = 0xEE0000EEU;
	b[1] = 0xEE0000EEU;
	b[2 + 2] = rx;                    /* rx_control.length (read=2, write=3? see struct: read, write, length) */
	b[2 + 5] = tx;
	b[buflen / 4 - 1] = 0xFF0000FFU;
	b[buflen / 4 - 2] = 0xFF0000FFU;
}

static int sweet_quiet;
static int sweet_prep_rc = -9;
#define SQ_PRINTF(...) do { if (!sweet_quiet) printf(__VA_ARGS__); } while (0)

static int
sweet_rings_init(void)
{
	/* 0268: ONE wcomb mapping for CCISM_MCU + CCISM_MCU_EXP (0x8e139000..0x8e20c000): 32 separate mappings exhausted the I/O VA
	 * space and the next ml_io_map panicked (q10bf: kmem_alloc(..,16384,..) failed with 3). */
	static volatile uint8_t *base;
	uintptr_t pa = 0x8e139000UL;
	unsigned q;

	if (base == NULL) {
		base = (volatile uint8_t *)ml_io_map_wcomb(0x8e139000UL, 0xd3000);
	}
	if (base == NULL) {
		SQ_PRINTF("NORD2 MODEM: ring area map failed\n");
		return -1;
	}
	for (q = 0; q < 16; q++) {
		uint32_t buflen = 40 + sweet_rxq[q] + sweet_txq[q];
		volatile uint32_t *b = (volatile uint32_t *)(base + (pa - 0x8e139000UL));
		sweet_ring_create(b, sweet_rxq[q], sweet_txq[q]);
		sweet_ring[q] = b + 2;        /* control block: rx{read,write,length}, tx{read,write,length} */
		sweet_ring_pa[q] = (uint32_t)pa;
		pa += buflen;
	}
	{
		static const uint32_t expsz[16] = { 12 << 10, 32 << 10, 8 << 10, 0, 0, 0, 8 << 10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		uintptr_t ea = 0x8e1ed400UL;
		unsigned e;

		for (e = 0; e < 16; e++) {
			uint32_t buflen = 40 + 2 * expsz[e];
			volatile uint32_t *eb = (volatile uint32_t *)(base + (ea - 0x8e139000UL));
			eb[0] = 0xEE0000EEU;
			eb[1] = 0xEE0000EEU;
			eb[2] = 0; eb[3] = 0; eb[4] = expsz[e];
			eb[5] = 0; eb[6] = 0; eb[7] = expsz[e];
			eb[buflen / 4 - 2] = 0xFF0000FFU;
			eb[buflen / 4 - 1] = 0xFF0000FFU;
			ea += buflen;
		}
		__asm__ volatile ("dsb sy" ::: "memory");
		SQ_PRINTF("NORD2 MODEM: rings: 16 + 16 exp created, exp end %#lx\n", (unsigned long)ea);
	}
	return 0;
}

/*
 * 0269: CCB control block, RAW_CCB_CTRL = AP 0x8e128000 / MD 0x40128000, 4 KiB, SMF_NCLR_FIRST (the Linux kernel never clears it).
 * The Linux KERNEL writes nothing here: port/port_smem.c CCB_CTRL_INFO only returns {ctrl_offset, ctrl_addr 0x8e128000, ctrl_length
 * 0x1000} and sets smem_port->state = CCB_USER_OK (AP-internal).  The headers are written by the vendor ccci_mdinit before
 * DO_START_MD, in user space: libccci_util.so ccci_ccb_init_users() (0x8218) -> for user 0/1/2 (DHL/MD_MONITOR/META, ctrl_offset
 * 0/18/19): CCB_CTRL_INFO, mmap(/dev/ccci_ccb_ctrl), then for every ccb_configs[] entry of that user one 64-byte struct buffer_header
 * (port/port_smem.h) at ctrl + index * 64:
 *   +0x00..+0x13 = 0 (dl guard_band, alloc, free, read, write), +0x14 dl_page_size, +0x18 dl_data_buffer_size, +0x1c 0xEEFF0011,
 *   +0x20..+0x33 = 0 (ul ...),                                    +0x34 ul_page_size, +0x38 ul_data_buffer_size, +0x3c 0xEEFF0011.
 * The MD checks these in ccci_shm_srv_init (task INITIAL) right after its NVRAM init (~809 FS ops): 0xFFFFFFFF there is
 * "Assert fail: ccci_shm_bm.c 603".  The Linux n2mdinit runs that stopped at 809 ops died on the same assert (dump_mine_stall.txt,
 * ccci_dump_ok2.txt run 1): n2mdinit did the ioctls but never wrote the mmap'ed headers.
 * Values = the 20 GET_CCB_CONFIG replies of the vendor run (ccci_trace_pristine.log) = port_smem.c ccb_configs[].
 */
static const uint32_t sweet_ccb_cfg[20][4] = {    /* { dl_page_size, ul_page_size, dl_buff_size, ul_buff_size } */
	{ 0x400, 0x400, 0x8000, 0x8000 },             /*  0 DHL ctrl */
	{ 0x5000, 0x80, 0x1e000, 0x80 },              /*  1 DHL exception */
	{ 0x200, 0x400, 0x9000, 0x8000 },             /*  2 DHL PS */
	{ 0x200, 0x80, 0x8000, 0x80 },                /*  3 DHL HWLOGGER1 */
	{ 0x200, 0x80, 0x8000, 0x80 },                /*  4 DHL HWLOGGER2 */
	{ 0x200, 0x80, 0x20000, 0x80 },               /*  5 DHL HWLOGGER3 */
	{ 0x80, 0x80, 0x80, 0x80 },                   /*  6..16 DHL dummy */
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x80, 0x80 },
	{ 0x80, 0x80, 0x280, 0x80 },                  /* 17 DHL dummy padding */
	{ 0x200, 0x400, 0x4000, 0x4000 },             /* 18 MD_MONITOR (user 1, ctrl_offset 18) */
	{ 0x10400, 0xfc00, 0x82000, 0x7e000 },        /* 19 META (user 2, ctrl_offset 19) */
};
static uint32_t sweet_ccb_was[3];                 /* page words +0x00/+0x18/+0x1c before the write (log) */

static int
sweet_ccb_ctrl_init(void)
{
	static volatile uint32_t *ctl;                /* the ONE 4 KiB wcomb mapping of 0x8e128000, kept */
	unsigned j, w;

	if (ctl == NULL) {
		ctl = (volatile uint32_t *)ml_io_map_wcomb(0x8e128000UL, 0x1000);
	}
	if (ctl == NULL) {
		return -1;
	}
	sweet_ccb_was[0] = ctl[0];
	sweet_ccb_was[1] = ctl[0x18 / 4];
	sweet_ccb_was[2] = ctl[0x1c / 4];
	for (j = 0; j < 20; j++) {                    /* ctrl_offset 0 (18 DHL entries), 18, 19: contiguous, 20 * 64 = 0x500 bytes */
		volatile uint32_t *h = ctl + j * 16;
		for (w = 0; w < 5; w++) {
			h[w] = 0;                             /* dl guard_band, alloc, free, read, write index */
			h[8 + w] = 0;                         /* ul guard_band, alloc, free, read, write index */
		}
		h[5] = sweet_ccb_cfg[j][0];               /* dl_page_size */
		h[6] = sweet_ccb_cfg[j][2];               /* dl_data_buffer_size */
		h[7] = 0xEEFF0011U;                       /* dl_guard_band_e */
		h[13] = sweet_ccb_cfg[j][1];              /* ul_page_size */
		h[14] = sweet_ccb_cfg[j][3];              /* ul_data_buffer_size */
		h[15] = 0xEEFF0011U;                      /* ul_guard_band_e */
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	for (j = 0; j < 20; j++) {                    /* read back the fields the MD validates */
		if (ctl[j * 16 + 6] != sweet_ccb_cfg[j][2] || ctl[j * 16 + 14] != sweet_ccb_cfg[j][3]) {
			return -2 - (int)j;
		}
	}
	return 0;
}

/*
 * 0254: AP-side CCCI service from HS2 up to READY, following Linux eccci (nord2-current-kernel/drivers/misc/mediatek/eccci):
 *  - RX  : hif/ccci_hif_ccif.c ccif_rx_collect + hif/ccci_ringbuf.c ccci_ringbuf_readable/read/move_rpointer, seq check as
 *          ccci_md_check_rx_seq_num; dispatch by ccci_header.channel like port/port_proxy.c (CONTROL_RX 0, SYSTEM_RX 2, FS_RX 14,
 *          RPC_RX 32, STATUS_RX 67).
 *  - TX  : md_ccif_op_send_skb + ccci_ringbuf_write ({0xAABBAABB,len} + packet padded to 8 + {0xCCDDEEFF x2}) into the tx half of the
 *          ring, seq/assert bit as ccci_md_inc_tx_seq_num (hif/ccci_hif_internal.h:155-178), doorbell as md_ccif_send (BUSY bit q,
 *          TCHNUM = H2D_RINGQ0 + q = q).  Linux skips the doorbell when BUSY is still set; we remember it and ring it when BUSY clears.
 *  - RPC : port/port_rpc.c rpc_msg_handler/ccci_rpc_work_helper with the values this phone's Linux gave (ccci_dump_ok2.txt) and its DT
 *          (oplus6893 cust.dtsi gpio_usage_mapping / md1_sim1_hot_plug_eint, LK md_attr_node); 0x400F/0x4010 are user-space
 *          (ccci_rpcd) ops, answered like n2mdinit.
 *  - SYS : port/port_sysmsg.c replies (only when READY, as proxy_send_msg_to_md).  CCISM_SHM_INIT is not sent: Linux only sends it at
 *          READY when the SCP reports itself up (fsm/ccci_fsm_scp.c), and nothing here talks to the SCP.
 *  - FS  : not served (ccci_fsd is user space); requests are only logged.  NOTE: on Linux HS2 arrives only after ~800 FS requests.
 * Ring memory is only touched through the wcomb ring pointers (packet-sized writes inside the tx halves; no bulk smem writes).
 */
#define SWEET_CH_MAX 257                          /* CCCI_MAX_CH_NUM */

static volatile uint32_t *sweet_ccif_ap;           /* AP CCIF0 (0x10209000), set by sweet_modem_start_inner */
static int sweet_seq_in[SWEET_CH_MAX];             /* traffic_info.seq_nums[IN]  (reset to -1) */
static uint32_t sweet_seq_out[SWEET_CH_MAX];       /* traffic_info.seq_nums[OUT] (reset to 0) */
static int sweet_md_ready;                         /* md_state READY (HS2 seen) */
static uint32_t sweet_tx_pending;                  /* doorbells skipped because BUSY was set */
static unsigned sweet_tick_ms;
static unsigned sweet_log_rx, sweet_log_rpc, sweet_log_sys, sweet_log_err;
static unsigned sweet_cnt_rx, sweet_cnt_tx, sweet_cnt_rpc, sweet_cnt_fs;

static uint32_t
sweet_rd32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
sweet_wr32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xff);
	p[1] = (uint8_t)((v >> 8) & 0xff);
	p[2] = (uint8_t)((v >> 16) & 0xff);
	p[3] = (uint8_t)((v >> 24) & 0xff);
}

/* strncmp(p, s, n) == 0 */
static int
sweet_strneq(const uint8_t *p, uint32_t n, const char *s)
{
	uint32_t i;

	for (i = 0; i < n; i++) {
		if (p[i] != (uint8_t)s[i]) {
			return 0;
		}
		if (s[i] == 0) {
			return 1;
		}
	}
	return 1;
}

/* the C string in p[0..n) (ends at NUL or n) equals s */
static int
sweet_streq(const uint8_t *p, uint32_t n, const char *s)
{
	uint32_t i;

	for (i = 0;; i++) {
		uint8_t c = i < n ? p[i] : 0;
		if (c != (uint8_t)s[i]) {
			return 0;
		}
		if (c == 0) {
			return 1;
		}
	}
}

/* printable copy of a name for the log */
static void
sweet_name_copy(char *o, unsigned max, const uint8_t *p, uint32_t n)
{
	unsigned i;

	for (i = 0; i + 1 < max && i < n && p[i] != 0; i++) {
		o[i] = (p[i] >= 32 && p[i] < 127) ? (char)p[i] : '?';
	}
	o[i] = 0;
}

static void
sweet_ccci_reset(void)
{
	unsigned i;

	for (i = 0; i < SWEET_CH_MAX; i++) {
		sweet_seq_in[i] = -1;                       /* ccci_reset_seq_num */
		sweet_seq_out[i] = 0;
	}
	sweet_seq_in[0] = 0;                                /* md_ccif_sram_rx_work checked HS1 (channel 0, seq 0, no assert bit) */
	sweet_md_ready = 0;
	sweet_tx_pending = 0;
}

/* md_ccif_send(): if (BUSY & (1 << ch)) skip, else BUSY = 1 << ch, TCHNUM = ch */
static void
sweet_ccif_kick(unsigned q)
{
	volatile uint32_t *ccif = sweet_ccif_ap;

	if (ccif == NULL) {
		return;
	}
	if (ccif[0x04 / 4] & (1U << q)) {
		sweet_tx_pending |= 1U << q;
		return;
	}
	ccif[0x04 / 4] = 1U << q;
	ccif[0x0C / 4] = q;
	__asm__ volatile ("dsb sy" ::: "memory");
	sweet_tx_pending &= ~(1U << q);
}

/* CCIF_RBF_WRITE with the 8-byte alignment of ccci_ringbuf_write (padding bytes written as 0) */
static void
sweet_ring_put(volatile uint8_t *buf, uint32_t len, uint32_t *pos, const uint8_t *src, uint32_t n, uint32_t adv)
{
	uint32_t i, w = *pos;

	for (i = 0; i < adv; i++) {
		buf[(w + i) % len] = i < n ? src[i] : 0;
	}
	*pos = (w + adv) % len;
}

/* md_ccif_op_send_skb on ring q: pkt = ccci_header (16 bytes, channel set by the caller) + payload */
static int
sweet_ring_tx(unsigned q, uint8_t *pkt, uint32_t plen)
{
	volatile uint32_t *c = q < 16 ? sweet_ring[q] : NULL;
	volatile uint8_t *buf;
	uint32_t rd, wr, len, need, room, ch;
	uint8_t h[8];

	if (q >= 16 || c == NULL || sweet_txq[q] == 0 || plen < 16) {
		return -1;
	}
	rd = c[3];
	wr = c[4];
	len = c[5];
	if (len != sweet_txq[q] || rd >= len || wr >= len) {
		if (sweet_log_err++ < 20) {
			printf("NORD2 MODEM: tx q%u bad control rd=%u wr=%u len=%u\n", q, rd, wr, len);
		}
		return -2;
	}
	need = (plen + 16 + 7) & ~7U;                      /* ccci_ringbuf_writeable */
	room = (rd == wr) ? len - 1 : (rd < wr ? len - wr - 1 + rd : rd - wr - 1);
	if (need >= room) {
		if (sweet_log_err++ < 20) {
			printf("NORD2 MODEM: tx q%u full (need %u room %u)\n", q, need, room);
		}
		return -3;
	}
	ch = sweet_rd32(pkt + 8) & 0xffffU;
	if (ch < SWEET_CH_MAX) {                            /* ccci_md_inc_tx_seq_num */
		uint32_t ab = 1;
		if ((ch == 33 /* CCCI_RPC_TX */ || ch == 15 /* CCCI_FS_TX */) && sweet_md_ready) {
			ab = 0;                             /* assert bit only while md_state == BOOT_WAITING_FOR_HS2 */
		}
		sweet_wr32(pkt + 8, ch | ((sweet_seq_out[ch] & 0x7fffU) << 16) | (ab << 31));
		sweet_seq_out[ch]++;
	}
	buf = (volatile uint8_t *)(c + 6) + c[2];           /* tx_buffer = buffer + rx_control.length */
	sweet_wr32(h, 0xAABBAABBU);
	sweet_wr32(h + 4, plen);
	sweet_ring_put(buf, len, &wr, h, 8, 8);
	sweet_ring_put(buf, len, &wr, pkt, plen, (plen + 7) & ~7U);
	sweet_wr32(h, 0xCCDDEEFFU);
	sweet_wr32(h + 4, 0xCCDDEEFFU);
	sweet_ring_put(buf, len, &wr, h, 8, 8);
	__asm__ volatile ("dsb sy" ::: "memory");
	c[4] = wr;                                          /* tx_control.write */
	__asm__ volatile ("dsb sy" ::: "memory");
	sweet_cnt_tx++;
	sweet_ccif_kick(q);
	return 0;
}

/* proxy_send_msg_to_md: header-only message {CCCI_MAGIC_NUM, msg, ch, resv} on the port's tx queue */
static void
sweet_send_msg(unsigned q, uint32_t ch, uint32_t msg, uint32_t resv)
{
	uint8_t m[16];
	int rc;

	sweet_wr32(m, 0xFFFFFFFFU);
	sweet_wr32(m + 4, msg);
	sweet_wr32(m + 8, ch);
	sweet_wr32(m + 12, resv);
	rc = sweet_ring_tx(q, m, 16);
	printf("NORD2 MODEM: tx msg ch%u 0x%x resv 0x%x on q%u -> %d\n", ch, msg, resv, q, rc);
}

/* ---- RPC (port_rpc.c) ---- */
static const struct {
	const char *md;
	const char *dts;
} sweet_gpio_conv[] = {                               /* gpio_mapping_table */
	{ "GPIO_FDD_Band_Support_Detection_1", "GPIO_FDD_BAND_SUPPORT_DETECT_1ST_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_2", "GPIO_FDD_BAND_SUPPORT_DETECT_2ND_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_3", "GPIO_FDD_BAND_SUPPORT_DETECT_3RD_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_4", "GPIO_FDD_BAND_SUPPORT_DETECT_4TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_5", "GPIO_FDD_BAND_SUPPORT_DETECT_5TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_6", "GPIO_FDD_BAND_SUPPORT_DETECT_6TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_7", "GPIO_FDD_BAND_SUPPORT_DETECT_7TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_8", "GPIO_FDD_BAND_SUPPORT_DETECT_8TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_9", "GPIO_FDD_BAND_SUPPORT_DETECT_9TH_PIN" },
	{ "GPIO_FDD_Band_Support_Detection_A", "GPIO_FDD_BAND_SUPPORT_DETECT_ATH_PIN" },
	{ "GPIO_RF_PWREN_RST_PIN", "GPIO_RF_PWREN_RST_PIN" },
};

static const struct {
	const char *name;
	int32_t md;                                    /* cell 1 of <&pio N 0> = MD view id */
} sweet_gpio_dt[] = {                                 /* gpio_usage_mapping (oplus6893 cust.dtsi; matches ccci_dump_ok2.txt) */
	{ "GPIO_SIM1_HOT_PLUG", 43 }, { "GPIO_SIM2_SCLK", 45 }, { "GPIO_SIM2_SRST", 46 }, { "GPIO_SIM2_SIO", 47 },
	{ "GPIO_SIM1_SIO", 48 }, { "GPIO_SIM1_SRST", 49 }, { "GPIO_SIM1_SCLK", 50 },
	{ "GPIO_FDD_BAND_SUPPORT_DETECT_1ST_PIN", 63 }, { "GPIO_FDD_BAND_SUPPORT_DETECT_2ND_PIN", 64 },
	{ "GPIO_FDD_BAND_SUPPORT_DETECT_3RD_PIN", 65 }, { "GPIO_FDD_BAND_SUPPORT_DETECT_4TH_PIN", 66 },
};

/* get_md_gpio_info: MD view id, or FS_FUNC_FAIL (-5) when the name is not in the DT (e.g. GPIO_SIM2_HOT_PLUG) */
static int32_t
sweet_gpio_md(const uint8_t *name, uint32_t n)
{
	const char *dts = NULL;
	unsigned i;

	for (i = 0; i < sizeof(sweet_gpio_conv) / sizeof(sweet_gpio_conv[0]); i++) {
		if (sweet_strneq(name, n, sweet_gpio_conv[i].md)) {
			dts = sweet_gpio_conv[i].dts;
			break;
		}
	}
	for (i = 0; i < sizeof(sweet_gpio_dt) / sizeof(sweet_gpio_dt[0]); i++) {
		if (dts != NULL ? sweet_streq((const uint8_t *)dts, 64, sweet_gpio_dt[i].name) : sweet_streq(name, n, sweet_gpio_dt[i].name)) {
			return sweet_gpio_dt[i].md;
		}
	}
	return -5;
}

/* get_md_gpio_val(num) = gpio_get_value(num): Linux GPIO number = pin + 29 on this phone ("92(63@md)" in the boot log); MT6885 DI
 * registers at 0x10005200 + 0x10 * (pin / 32) (pinctrl-mt6885.c mt6885_pin_di_range) */
static int32_t
sweet_gpio_val(uint32_t num)
{
	static volatile uint32_t *gpio;
	uint32_t pin;

	if (num < 29 || num - 29 > 219) {
		return 0;
	}
	pin = num - 29;
	if (gpio == NULL) {
		gpio = (volatile uint32_t *)ml_io_map(0x10005000UL, 0x1000);
	}
	if (gpio == NULL) {
		return 0;
	}
	return (int32_t)((gpio[(0x200 + (pin / 32) * 0x10) / 4] >> (pin % 32)) & 1U);
}

/* get_eint_attr_DTSVal: MD1_SIM1_HOT_PLUG_EINT {interrupts <0 8>, debounce <0 10000>, dedicated <0 0>, src_pin <0 1>, sockettype <0 0>};
 * MD1_SIM2_HOT_PLUG_EINT exists without properties (every value -12 -> ERR_SIM_HOT_PLUG_QUERY_STRING), SIM3/SIM4 nodes absent */
static int32_t
sweet_eint_attr(const uint8_t *name, uint32_t n, uint32_t type, int32_t *val)
{
	/* SIM_EINT_NUM, DEBOUNCE (us/1000), POLA, SENS (IRQ_TYPE_LEVEL_LOW: 0, 0), SOCKE, DEDICATEDEN, SRCPIN */
	static const int32_t sim1[7] = { 0, 10, 0, 0, 0, 0, 1 };

	if (type >= 7) {
		return -12;                                 /* ERR_SIM_HOT_PLUG_QUERY_TYPE */
	}
	if (sweet_strneq(name, n, "MD1_SIM1_HOT_PLUG_EINT")) {
		*val = sim1[type];
		return 0;
	}
	return -11;                                         /* ERR_SIM_HOT_PLUG_QUERY_STRING */
}

static uint8_t sweet_rpc_out[16 + 2048];
static uint32_t sweet_rpc_olen, sweet_rpc_oargc;

static void
sweet_rpc_arg(const uint8_t *d, uint32_t n)
{
	uint32_t i, al = (n + 3) & ~3U;

	if (sweet_rpc_olen + 4 + al > sizeof(sweet_rpc_out)) {
		return;
	}
	sweet_wr32(sweet_rpc_out + sweet_rpc_olen, n);
	for (i = 0; i < al; i++) {
		sweet_rpc_out[sweet_rpc_olen + 4 + i] = i < n ? d[i] : 0;
	}
	sweet_rpc_olen += 4 + al;
	sweet_rpc_oargc++;
}

static void
sweet_rpc_u32(uint32_t v)
{
	uint8_t b[4];

	sweet_wr32(b, v);
	sweet_rpc_arg(b, 4);
}

/* IPC_RPC_GET_GPIO_ADC_OP (ccci_rpc_get_gpio_adc / _v2); in = 157 (v1, 3 pins) or 425 (v2, 10 pins) bytes */
static void
sweet_rpc_gpio_adc(const uint8_t *in, uint32_t inlen)
{
	uint8_t out[88];
	int v2 = inlen == 425;
	uint32_t cnt = v2 ? 10 : 3, i, req, vm, outlen = v2 ? 88 : 32;
	const uint8_t *names = in + (v2 ? 4 : 2), *pnum = names + cnt * 34;
	uint8_t *o_num = out, *o_val = out + cnt * 4, *o_adc = out + cnt * 8;

	for (i = 0; i < sizeof(out); i++) {
		out[i] = 0x0F;                              /* "0xF for failure" */
	}
	req = v2 ? ((uint32_t)in[0] | ((uint32_t)in[1] << 8)) : in[0];
	vm = v2 ? ((uint32_t)in[2] | ((uint32_t)in[3] << 8)) : in[1];
	for (i = 0; i < cnt; i++) {
		int32_t md;
		if (!(vm & (1U << i))) {
			continue;
		}
		if (req & 1U) {                             /* RPC_REQ_GPIO_PIN (alone or with VALUE) */
			md = sweet_gpio_md(names + i * 34, 34);
			if (md >= 0) {
				sweet_wr32(o_num + i * 4, (uint32_t)md);
				if (req & 2U) {
					sweet_wr32(o_val + i * 4, (uint32_t)sweet_gpio_val((uint32_t)md + 29));
				}
			}
		} else if (req & 2U) {                      /* RPC_REQ_GPIO_VALUE only: gpioPinNum[] given by the MD */
			sweet_wr32(o_val + i * 4, (uint32_t)sweet_gpio_val(sweet_rd32(pnum + i * 4)));
		}
	}
	if (req & 0x10U) {
		sweet_wr32(o_adc, 2);                       /* ccci_get_adc_num(): md_auxadc io-channels <&auxadc 2> */
	}
	if (req & 0x20U) {
		sweet_wr32(o_adc + 4, 0);                   /* adcChMeasSum: cached raw value not known here (0) */
	}
	sweet_rpc_u32(0);
	sweet_rpc_arg(out, outlen);
}

static void
sweet_rpc(const uint8_t *req, uint32_t plen)
{
	struct {
		const uint8_t *p;
		uint32_t len;
	} a[6];
	uint32_t op, argc, off, i, type = 0;
	int32_t v = 0;
	char nm[48];
	int rc;

	nm[0] = 0;
	sweet_cnt_rpc++;
	if (plen < 24 || plen > 2048) {                     /* RPC_MAX_BUF_SIZE */
		printf("NORD2 MODEM: rpc drop len %u\n", plen);
		return;
	}
	op = sweet_rd32(req + 16);
	argc = sweet_rd32(req + 20);
	if (sweet_rd32(req + 12) > 2 || argc > 6) {         /* RPC_REQ_BUFFER_NUM, RPC_MAX_ARG_NUM */
		printf("NORD2 MODEM: rpc drop op %x idx %u argc %u\n", op, sweet_rd32(req + 12), argc);
		return;
	}
	for (i = 0, off = 24; i < argc; i++) {
		uint32_t l;
		if (off + 4 > plen) {
			printf("NORD2 MODEM: rpc drop op %x short\n", op);
			return;
		}
		l = sweet_rd32(req + off);
		if (l >= plen || off + 4 + l > plen) {
			printf("NORD2 MODEM: rpc drop op %x arg%u len %u\n", op, i, l);
			return;
		}
		a[i].p = req + off + 4;
		a[i].len = l;
		off += 4 + ((l + 3) & ~3U);
	}
	for (i = 0; i < 16; i++) {
		sweet_rpc_out[i] = req[i];                  /* data[0] and reserved are echoed */
	}
	sweet_rpc_olen = 24;
	sweet_rpc_oargc = 0;
	switch (op) {
	case 0x4001:                                        /* IPC_RPC_GET_TDD_EINT_NUM_OP */
	case 0x4002:                                        /* IPC_RPC_GET_GPIO_NUM_OP */
	case 0x4003:                                        /* IPC_RPC_GET_ADC_NUM_OP */
		if (argc < 2 || a[0].len < 1) {
			v = -2;                             /* FS_PARAM_ERROR */
		} else {
			sweet_name_copy(nm, sizeof(nm), a[0].p, a[0].len);
			v = op == 0x4002 ? sweet_gpio_md(a[0].p, a[0].len) : (op == 0x4003 ? 2 : -5);
		}
		sweet_rpc_u32((uint32_t)v);
		sweet_rpc_u32((uint32_t)v);
		break;
	case 0x4004:                                        /* IPC_RPC_GET_EMI_CLK_TYPE_OP: get_dram_type_clk() fails */
		v = argc != 0 ? -2 : -5;
		sweet_rpc_u32((uint32_t)v);
		sweet_rpc_u32((uint32_t)v);
		sweet_rpc_u32((uint32_t)v);
		break;
	case 0x4005:                                        /* IPC_RPC_GET_EINT_ATTR_OP: name, ?, type */
		if (argc < 3 || a[0].len < 1) {
			v = -2;
			sweet_rpc_u32((uint32_t)v);
			sweet_rpc_u32((uint32_t)v);
		} else {
			int32_t val = 0;
			type = a[2].len >= 4 ? sweet_rd32(a[2].p) : 0;
			sweet_name_copy(nm, sizeof(nm), a[0].p, a[0].len);
			v = sweet_eint_attr(a[0].p, a[0].len, type, &val);
			if (v == 0) {
				sweet_rpc_u32(0);
				sweet_rpc_u32((uint32_t)val);
				v = val;
			} else {
				sweet_rpc_u32((uint32_t)v);
				sweet_rpc_u32((uint32_t)v);
			}
		}
		break;
	case 0x4006:                                        /* IPC_RPC_GET_GPIO_VAL_OP */
	case 0x4007:                                        /* IPC_RPC_GET_ADC_VAL_OP */
		if (argc != 1) {
			v = -2;
		} else {
			uint32_t num = a[0].len >= 4 ? sweet_rd32(a[0].p) : 0;
			v = op == 0x4006 ? sweet_gpio_val(num) : 0;   /* ADC: ccci_get_adc_val() cached raw value unknown -> 0 */
		}
		sweet_rpc_u32((uint32_t)v);
		sweet_rpc_u32((uint32_t)v);
		break;
	case 0x4009:                                        /* IPC_RPC_GET_GPIO_ADC_OP */
		if (argc != 1 || (a[0].len != 157 && a[0].len != 425)) {
			v = -2;
			sweet_rpc_u32((uint32_t)v);
			sweet_rpc_u32((uint32_t)v);
		} else {
			sweet_rpc_gpio_adc(a[0].p, a[0].len);
		}
		break;
	case 0x400D:                                        /* IPC_RPC_CCCI_LHIF_MAPPING (AP-local remap only) */
		v = argc != 1 ? -2 : 0;
		sweet_rpc_u32((uint32_t)v);
		sweet_rpc_u32((uint32_t)v);
		break;
	case 0x400E:                                        /* IPC_RPC_DTSI_QUERY_OP: {u8 req, u8 index, char strName[64]} -> {u32, char[64]} */
		if (argc != 1) {
			v = -2;
			sweet_rpc_u32((uint32_t)v);
			sweet_rpc_u32((uint32_t)v);
		} else {
			uint8_t o[68];
			uint32_t val = 0x0F0F0F0FU, nlen = a[0].len > 2 ? a[0].len - 2 : 0;
			for (i = 0; i < sizeof(o); i++) {
				o[i] = 0x0F;
			}
			if (nlen > 64) {
				nlen = 64;
			}
			sweet_name_copy(nm, sizeof(nm), a[0].p + 2, nlen);
			if (a[0].len >= 1 && a[0].p[0] == 1 /* RPC_REQ_PROP_VALUE */) {
				if (sweet_streq(a[0].p + 2, nlen, "mediatek,md_drdi_rf_set_idx")) {
					val = 1;
				} else if (sweet_streq(a[0].p + 2, nlen, "mediatek,md_product_name_model_id")) {
					val = 0x515b;
				}
			}
			sweet_wr32(o, val);
			sweet_rpc_u32(0);
			sweet_rpc_arg(o, sizeof(o));
			v = (int32_t)val;
		}
		break;
	case 0x400F:                                        /* IPC_RPC_QUERY_AP_SYS_PROPERTY (ccci_rpcd): property not set */
		if (argc >= 1) {
			sweet_name_copy(nm, sizeof(nm), a[0].p, a[0].len);
		}
		sweet_rpc_u32(0);
		sweet_rpc_arg((const uint8_t *)"", 1);
		break;
	case 0x4013:                                        /* IPC_RPC_QUERY_CARD_TYPE: Linux only logs; the request params go back */
		for (i = 0; i < argc; i++) {
			sweet_rpc_arg(a[i].p, a[i].len);
		}
		break;
	case 0x4321:                                        /* IPC_RPC_IT_OP */
		sweet_rpc_u32(1);
		sweet_rpc_u32(0xA5A5);
		break;
	default:                                            /* 0x4008 (no FEATURE_RF_CLK_BUF), 0x4010 (ccci_rpcd), ...: FS_NO_OP */
		v = -1;
		sweet_rpc_u32((uint32_t)v);
		break;
	}
	sweet_wr32(sweet_rpc_out + 4, sweet_rpc_olen);      /* header.data[1] = packet length */
	sweet_wr32(sweet_rpc_out + 8, 33);                  /* CCCI_RPC_TX (seq/assert filled by sweet_ring_tx) */
	sweet_wr32(sweet_rpc_out + 16, op | 0xFFFF0000U);   /* RPC_API_RESP_ID */
	sweet_wr32(sweet_rpc_out + 20, sweet_rpc_oargc);
	rc = sweet_ring_tx(1, sweet_rpc_out, sweet_rpc_olen); /* ccci_rpc port: txq 1 */
	if (sweet_log_rpc < 60) {                           /* type: EINT attribute type (0x4005 only) */
		sweet_log_rpc++;
		printf("NORD2 MODEM: rpc t=%u op %x argc %u %s/%u -> %d (%u B, tx %d)\n", sweet_tick_ms, op, argc, nm, type, v,
		    sweet_rpc_olen, rc);
	}
}

/* 0258: in-kernel ccci_fs server (embedded NVRAM tree, RAM overlay) */
#include <libkern/zlib.h>
#include <kern/kalloc.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include "sweet_nvram_blob.h"

/* SWEET_FS_GLUE_BEGIN */
#define SWEET_FS_ROOTFS "/private/var/db/nord2-modem"   /* contents not embedded: SWEET_FS_ROOTFS + canonical path, e.g. .../mnt/vendor/nvdata/md/NVRAM/CALIBRAT/EA57_000 */

static void *
sweet_fs_alloc(uint32_t size)
{
	return kalloc_data(size, Z_WAITOK | Z_ZERO);
}

static void
sweet_fs_free(void *p, uint32_t size)
{
	kfree_data(p, size);
}

static int
sweet_fs_rootfs_read(const char *path, uint8_t *buf, uint32_t size)
{
	char full[320];
	vnode_t vp = NULLVP;
	vfs_context_t ctx = vfs_context_kernel();
	int err, resid = 0;

	snprintf(full, sizeof(full), "%s%s", SWEET_FS_ROOTFS, path);
	err = vnode_open(full, FREAD, 0, 0, &vp, ctx);
	if (err) {
		return -1;
	}
	err = vn_rdwr(UIO_READ, vp, (caddr_t)buf, (int)size, 0, UIO_SYSSPACE, IO_NODELOCKED | IO_UNIT, vfs_context_ucred(ctx), &resid,
	    vfs_context_proc(ctx));
	vnode_close(vp, FREAD, ctx);
	return (err || resid) ? -1 : 0;
}

/* 0261: raw reads of the NVRAM blob in the userdata tail: /dev/rufsroot0 is the sweet_ufs character device of the Android
 * 'userdata' partition (LUN2, GPT entry 56) that holds the 16 GiB HFSX iOS root at its start; the blob sits at 64 GiB. */
#define SWEET_FS_DISK_DEV "/dev/rufsroot0"
#define SWEET_FS_DISK_OFF (64ULL << 30)
static vnode_t sweet_fs_dvp = NULLVP;              /* 0270: opened lazily on the first disk-blob need, kept (usecount only) */
/*
 * 0715 (sim-20261115): the raw copy at userdata+64 GiB is no longer safe: since the iOS root volume was grown to 107 GiB (storage-20261114,
 * 2026-11-14) that region is FREE SPACE of the HFS volume, and iOS reused it (q10jfinal18bt3 boot bt18c: "fs disk blob: no valid
 * header (377f0682)", the modem never reached READY, no AT, no +CGMR: Settings 'Modem Firmware' empty, no IMEI). The blob is now
 * first looked for as a regular file of the root volume, SWEET_FS_FILE (HFS-owned, never overwritten by allocation; same N2NVDSK1
 * format, offsets from 0); if it is absent or its header/table is invalid the raw copy is used exactly as before. kern.n2nv_src
 * (ro): 0 none yet / unusable, 1 raw userdata+64 GiB, 2 rootfs file; +0x100 when the file was present but invalid.
 */
#define SWEET_FS_FILE "/private/var/db/nord2-modem/n2nvdisk.bin"
static uint64_t sweet_fs_disk_off = SWEET_FS_DISK_OFF;
static int sweet_fs_src;                           /* 0 none, 1 raw device, 2 rootfs file */
static int sweet_fs_file_tried, sweet_fs_file_bad;
unsigned nord2_nv_src;
static void sweet_fs_dev_close(void);
static unsigned sweet_fs_dev_fails;

/*
 * 0270: open /dev/rufsroot0 once, on the first file that is neither embedded nor in RAM.  Safe from the modem thread call:
 *  - namei() with vfs_context_kernel() before the root mount returns ENOENT ("rootvnode is not set"), it does not panic or block;
 *    devfs is mounted on /dev right after the root (bsd_init), and sweet_ufs made the node at probe time;
 *  - spec_open(VCHR, FREAD) has no mounted-on check (only FWRITE does) and sweet_ufs_open() only checks su.ready (ENXIO);
 *  - reads go physio() -> sweet_ufs_strategy(), synchronous polled UFS commands in this thread under sweet_ufs_lock (it sleeps
 *    between polls when preemption is enabled, bounded by the per-command timeout); no completion thread, no lock held by us.
 * The open reference (usecount) is kept; each read takes its own iocount (vnode_getwithref) so no iocount is held between reads.
 * A failed open is retried for up to 3 s (root not mounted yet / UFS not ready) and the whole thing gives up after 3 failed needs.
 */
static int
sweet_fs_dev_open(void)
{
	vfs_context_t ctx = vfs_context_kernel();
	unsigned t;
	int err = 0;

	if (sweet_fs_dvp != NULLVP) {
		return 0;
	}
	if (!sweet_fs_file_tried) {                        /* 0715: the root volume's file first */
		vnode_t fvp = NULLVP;
		err = vnode_open(SWEET_FS_FILE, FREAD, 0, 0, &fvp, ctx);
		if (err == 0) {
			vnode_put(fvp);
			sweet_fs_dvp = fvp;
			sweet_fs_disk_off = 0;
			sweet_fs_src = 2;
			sweet_fs_file_tried = 1;
			printf("NORD2 MODEM: fs disk blob source: rootfs file %s\n", SWEET_FS_FILE);
			return 0;
		}
		if (err != ENOENT) {                           /* ENOENT may be "root not mounted yet": decided below (0715) */
			sweet_fs_file_tried = 1;
			printf("NORD2 MODEM: fs disk blob: rootfs file %s not usable (%d), raw userdata+64GiB\n", SWEET_FS_FILE, err);
		}
	}
	if (sweet_fs_dev_fails >= 3) {
		return -1;
	}
	for (t = 0; t < 30; t++) {
		vnode_t vp = NULLVP;
		err = vnode_open(SWEET_FS_DISK_DEV, FREAD, 0, 0, &vp, ctx);
		if (err == 0) {
			vnode_put(vp);                      /* drop the iocount, keep the open usecount */
			sweet_fs_dvp = vp;
			sweet_fs_dev_fails = 0;
			sweet_fs_disk_off = SWEET_FS_DISK_OFF;   /* 0715 */
			sweet_fs_src = 1;
			if (!sweet_fs_file_tried) {             /* /dev is there, so the root is mounted now: the file once more */
				vnode_t fvp = NULLVP;
				sweet_fs_file_tried = 1;
				if (vnode_open(SWEET_FS_FILE, FREAD, 0, 0, &fvp, ctx) == 0) {
					vnode_put(fvp);
					sweet_fs_dev_close();
					sweet_fs_dvp = fvp;
					sweet_fs_disk_off = 0;
					sweet_fs_src = 2;
					printf("NORD2 MODEM: fs disk blob source: rootfs file %s\n", SWEET_FS_FILE);
					return 0;
				}
				printf("NORD2 MODEM: fs disk blob: no rootfs file %s, raw userdata+64GiB\n", SWEET_FS_FILE);
			}
			return 0;
		}
		if (err != ENOENT && err != ENXIO) {
			break;
		}
		delay_for_interval(100, NSEC_PER_MSEC);
	}
	sweet_fs_dev_fails++;
	printf("NORD2 MODEM: fs disk blob: open %s failed (%d), %u/3\n", SWEET_FS_DISK_DEV, err, sweet_fs_dev_fails);
	return -1;
}

static int
sweet_fs_dev_read(uint64_t off, void *buf, uint32_t len)
{
	vfs_context_t ctx = vfs_context_kernel();
	int err, resid = 0;

	if (sweet_fs_dvp == NULLVP || (off & 4095) || (len & 4095)) {
		return -1;
	}
	if (vnode_getwithref(sweet_fs_dvp)) {
		return -1;
	}
	err = vn_rdwr(UIO_READ, sweet_fs_dvp, (caddr_t)buf, (int)len, (off_t)(sweet_fs_disk_off + off), UIO_SYSSPACE, IO_NODELOCKED | IO_UNIT,
	    vfs_context_ucred(ctx), &resid, vfs_context_proc(ctx));
	vnode_put(sweet_fs_dvp);
	return (err || resid) ? -1 : 0;
}

static void
sweet_fs_dev_close(void)
{
	if (sweet_fs_dvp != NULLVP) {
		if (vnode_getwithref(sweet_fs_dvp) == 0) {
			vnode_close(sweet_fs_dvp, FREAD, vfs_context_kernel());   /* VNOP_CLOSE + usecount + this iocount */
		}
		sweet_fs_dvp = NULLVP;
	}
}

/* free bytes in the tx half of ring q (ccci_ringbuf_writeable) */
static uint32_t
sweet_ring_room(unsigned q)
{
	volatile uint32_t *c = sweet_ring[q];
	uint32_t rd, wr, len;

	if (c == NULL) {
		return 0;
	}
	rd = c[3];
	wr = c[4];
	len = c[5];
	if (len == 0 || rd >= len || wr >= len) {
		return 0;
	}
	return rd == wr ? len - 1 : (rd < wr ? len - wr - 1 + rd : rd - wr - 1);
}

/* FS_TX on ring q4; waits (up to ~2 s) for the modem to drain the ring when a multi-packet reply does not fit */
static int
sweet_fs_send(uint8_t *pkt, uint32_t len)
{
	uint32_t need = (len + 16 + 7) & ~7U;
	unsigned t;

	sweet_wr32(pkt + 8, 15);                            /* CCCI_FS_TX (seq/assert bit: sweet_ring_tx) */
	for (t = 0; t < 20000 && sweet_ring_room(4) <= need; t++) {
		if (sweet_tx_pending & (1U << 4)) {
			sweet_ccif_kick(4);
		}
		delay_for_interval(100, NSEC_PER_USEC);
	}
	return sweet_ring_tx(4, pkt, len);
}
/* SWEET_FS_GLUE_END */

/* SWEET_FS_CORE_BEGIN */
/*
 * 0258: in-kernel ccci_fs server (channel 14 FS_RX / 15 FS_TX, ring q4 = port_cfg.c DATA_FSD_Q), a port of n2mdinit fs_one()/serve_fs()
 * (gpu/build/baseband-recon-20261104/n2mdinit/n2mdinit.c, the user-space server that took the modem to READY) onto an embedded tree:
 *  - sweet_nvram_blob.h (generated by gen_blob.py): every file/dir of /mnt/vendor/{nvdata,protect_f,protect_s}/md, /vendor/etc/md and
 *    /vendor/etc/mdota (size, dir flag, directory order as ext4 listed it), raw-deflate contents of the files the modem reads, inflated on
 *    first use with libkern zlib into a kalloc'd buffer (clean buffers are evicted LRU above SWEET_FS_CACHE_MAX).
 *  - files whose contents are not embedded are read once from the root file system at SWEET_FS_ROOTFS + canonical path (sweet_fs_rootfs_read);
 *    if that fails a read/0x1022 returns -1/-9 and is logged as "nocontent".
 *  - modem writes/creates/deletes go to a RAM overlay (kalloc, capped by SWEET_FS_DIRTY_MAX); nothing is written back.
 * Packets: {u32 data0 (bit31 = more), u32 len, u32 ch|seq, u32 client} + {u32 op, u32 argc, [u32 len, bytes pad 4]...}; requests and replies
 * larger than one packet (3476 bytes) are split, each continuation repeating the op id (n2mdinit r_send).  UTF-16LE DOS paths X:/Y:/Z:/R:/Q:/
 * S:/T: map as in n2mdinit dos2linux().
 */
#define SWEET_FS_PKT 3476                         /* CCCI_MTU + 16-byte header + op id */
#define SWEET_FS_BUF (32 * 1024)                  /* largest request/reply in the vendor READY trace: 16.5 KB */
#define SWEET_FS_NH 128                           /* handles 1..127, lowest free first (n2mdinit halloc) */
#define SWEET_FS_MAXENT 80
#define SWEET_FS_DYN 256                          /* files the modem may create */
#define SWEET_FS_NMAX (SWEET_NV_NODES + SWEET_FS_DYN)
#define SWEET_FS_CACHE_MAX (6U * 1024 * 1024)     /* clean (re-inflatable) contents kept in RAM */
#define SWEET_FS_DIRTY_MAX (24U * 1024 * 1024)    /* modem writes kept in RAM */
#define SWEET_FS_TIME_M 0x6ab9abfdU               /* 0x1025 times as the vendor READY run returned them (vendor image mtime) */
#define SWEET_FS_TIME_C 0x4b48616bU

struct sweet_fs_rt {                              /* runtime state of a node */
	uint8_t *buf;                             /* contents (size bytes) when loaded */
	uint32_t cap;                             /* allocation size of buf */
	uint32_t size;                            /* current size (overlay) */
	uint32_t lru;
	uint16_t parent;                          /* dynamic nodes */
	uint8_t state;                            /* 0 not loaded, 1 clean, 2 dirty */
	uint8_t flags;                            /* 1 deleted, 2 dynamic, 4 dir, 8 nocontent logged */
	uint32_t name;                            /* dynamic nodes: offset in sweet_fs_dname */
};
static struct sweet_fs_rt sweet_fs_rt[SWEET_FS_NMAX];
static unsigned sweet_fs_ndyn;
static char sweet_fs_dname[SWEET_FS_DYN * 24];
static unsigned sweet_fs_dname_used;
static uint32_t sweet_fs_lru_clock, sweet_fs_clean_bytes, sweet_fs_dirty_bytes;

struct sweet_fs_h {
	uint8_t used, kind, wr;                   /* kind 1 file, 2 directory (0x900 open), 3 find */
	int32_t node;
	uint32_t pos;
	int16_t ent[SWEET_FS_MAXENT];             /* find: entries */
	int16_t nent, cur;
};
static struct sweet_fs_h sweet_fs_h[SWEET_FS_NH];

static uint8_t sweet_fs_req[SWEET_FS_BUF + 64], sweet_fs_rep[SWEET_FS_BUF + 64];
static uint32_t sweet_fs_acc, sweet_fs_rlen;
static unsigned sweet_fs_nops, sweet_fs_nerr, sweet_fs_nlog, sweet_fs_opcnt[48];

static void *sweet_fs_alloc(uint32_t size);
static void sweet_fs_free(void *p, uint32_t size);
static int sweet_fs_rootfs_read(const char *path, uint8_t *buf, uint32_t size);
static int sweet_fs_send(uint8_t *pkt, uint32_t len);
static int sweet_fs_inflate(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen);
static int sweet_fs_dev_open(void);
static int sweet_fs_dev_read(uint64_t off, void *buf, uint32_t len);
static void sweet_fs_dev_close(void);

static uint32_t
sweet_fs_rd(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
sweet_fs_wr(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void
sweet_fs_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
	while (n--) {
		*d++ = *s++;
	}
}

static void
sweet_fs_zero(uint8_t *d, uint32_t n)
{
	while (n--) {
		*d++ = 0;
	}
}

static const char *
sweet_fs_name(int32_t n)
{
	return n < SWEET_NV_NODES ? &sweet_nv_names[sweet_nv_node[n].name] : &sweet_fs_dname[sweet_fs_rt[n].name];
}

static int
sweet_fs_isdir(int32_t n)
{
	return n < SWEET_NV_NODES ? (sweet_nv_node[n].flags & SWEET_NV_DIR) != 0 : (sweet_fs_rt[n].flags & 4) != 0;
}

static int32_t
sweet_fs_parent(int32_t n)
{
	uint16_t p = n < SWEET_NV_NODES ? sweet_nv_node[n].parent : sweet_fs_rt[n].parent;
	return p == 0xFFFF ? -1 : (int32_t)p;
}

static int
sweet_fs_alive(int32_t n)
{
	return n >= 0 && n < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn) && !(sweet_fs_rt[n].flags & 1);
}

static uint32_t
sweet_fs_size(int32_t n)
{
	return sweet_fs_isdir(n) ? 4096 : sweet_fs_rt[n].size;
}

static int
sweet_fs_seg_eq(const char *a, unsigned n, const char *s)
{
	unsigned i;

	for (i = 0; i < n; i++) {
		if (s[i] != a[i]) {
			return 0;
		}
	}
	return s[n] == 0;
}

/* canonical path -> node (only live nodes), -1 if missing */
static int32_t sweet_fs_lookup(const char *path);
static int32_t
sweet_fs_lookup0(const char *path)
{
	int32_t cur = -1, i;
	const char *p = path;

	for (i = 0; i < SWEET_NV_NODES; i++) {
		const char *r;
		unsigned k;
		if (!(sweet_nv_node[i].flags & SWEET_NV_ROOT)) {
			continue;
		}
		r = sweet_fs_name(i);
		for (k = 0; r[k] && path[k] == r[k]; k++) {
		}
		if (r[k] == 0 && (path[k] == 0 || path[k] == '/')) {
			cur = i;
			p = path + k;
			break;
		}
	}
	if (cur < 0 || !sweet_fs_alive(cur)) {
		return -1;
	}
	while (*p == '/') {
		const char *s = ++p;
		unsigned n = 0;
		int32_t j, found = -1;
		while (s[n] && s[n] != '/') {
			n++;
		}
		if (n == 0) {
			continue;
		}
		if (!sweet_fs_isdir(cur)) {
			return -1;
		}
		for (j = cur + 1; j < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn); j++) {
			if (sweet_fs_parent(j) == cur && sweet_fs_alive(j) && sweet_fs_seg_eq(s, n, sweet_fs_name(j))) {
				found = j;
				break;
			}
		}
		if (found < 0) {
			return -1;
		}
		cur = found;
		p = s + n;
	}
	return cur;
}

/* n2mdinit dos2linux(): UTF-16LE "D:\a\b" -> canonical path; 0 ok, -1 bad */
static int
sweet_fs_dos2linux(const uint8_t *a, uint32_t alen, char *out, unsigned max)
{
	char tmp[256];
	unsigned n = 0, o = 0, i, skip;
	int last = '/';
	const char *root;

	for (i = 0; i + 1 < alen && n < 250; i += 2) {
		unsigned c = (unsigned)a[i] | ((unsigned)a[i + 1] << 8);
		if (c == 0) {
			break;
		}
		tmp[n++] = c < 128 ? (char)c : '?';
	}
	tmp[n] = 0;
	for (i = 0; i < n && i + 2 < max; i++) {            /* 0277bb: the raw DOS path stays in out for the fs log on failure */
		out[i] = tmp[i];
	}
	out[i < max ? i : max - 1] = 0;
	if (n < 2 || tmp[1] != ':') {
		return -1;
	}
	skip = 2;
	switch (tmp[0] | 0x20) {
	case 'x': root = "/mnt/vendor/protect_f/md"; break;
	case 'y': root = "/mnt/vendor/protect_s/md"; break;
	case 'z': root = "/mnt/vendor/nvdata/md"; break;
	case 'r': case 'q': root = "/vendor/etc/md"; break;
	case 's': root = "/data/vendor_de/md/ota_s"; break;
	case 't': root = "/vendor/etc/mdota"; break;
	default: return -1;
	}
	for (i = 0; root[i] && o < max - 2; i++) {
		out[o++] = root[i];
	}
	out[o++] = '/';
	for (i = skip; i < n && o < max - 1; i++) {
		char c = tmp[i] == '\\' ? '/' : tmp[i];
		if (c == '/' && last == '/') {
			continue;
		}
		out[o++] = c;
		last = c;
	}
	while (o > 1 && out[o - 1] == '/') {
		o--;
	}
	out[o] = 0;
	return 0;
}

/* ---- contents ---- */
static void
sweet_fs_evict(uint32_t need)
{
	while (sweet_fs_clean_bytes + need > SWEET_FS_CACHE_MAX) {
		int32_t i, v = -1;
		for (i = 0; i < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn); i++) {
			if (sweet_fs_rt[i].state == 1 && (v < 0 || sweet_fs_rt[i].lru < sweet_fs_rt[v].lru)) {
				v = i;
			}
		}
		if (v < 0) {
			return;
		}
		sweet_fs_free(sweet_fs_rt[v].buf, sweet_fs_rt[v].cap);
		sweet_fs_clean_bytes -= sweet_fs_rt[v].cap;
		sweet_fs_rt[v].buf = NULL;
		sweet_fs_rt[v].cap = 0;
		sweet_fs_rt[v].state = 0;
	}
}

static void
sweet_fs_path(int32_t n, char *out, unsigned max)
{
	int32_t chain[16];
	int k = 0;
	unsigned o = 0;

	while (n >= 0 && k < 16) {
		chain[k++] = n;
		n = sweet_fs_parent(n);
	}
	while (k-- > 0) {
		const char *s = sweet_fs_name(chain[k]);
		if (o && o < max - 1) {
			out[o++] = '/';
		}
		while (*s && o < max - 1) {
			out[o++] = *s++;
		}
	}
	out[o] = 0;
}

/*
 * 0261: contents that are not embedded come from a blob written raw into the Android 'userdata' partition (the sweet_ufs root
 * device, LUN2) at SWEET_FS_DISK_OFF, beyond the 16 GiB HFSX iOS root volume.  Built by mk_nvdisk.py; read with sweet_fs_dev_read()
 * (4 KiB aligned).  Layout (little endian): header block {"N2NVDSK1", u32 version 1, u32 count, u32 table_bytes, u32 table_crc,
 * u64 data_bytes, u32 header_crc (of bytes 0..63 with this field 0)}; table at 4096: count x 128 bytes {char path[96], u32 size,
 * u32 clen, u64 offset, u32 crc (stored bytes), u32 flags (1 = raw deflate), u32 raw_crc, u32 0}; data 4 KiB aligned.
 */
static uint8_t *sweet_fs_dtab;
static uint32_t sweet_fs_dcount, sweet_fs_dtab_cap;
static int sweet_fs_dstate;                       /* 0 untried, 1 ready, -1 unavailable */
static unsigned sweet_fs_dlog, sweet_fs_dio_fails;

static uint32_t
sweet_fs_crc32(const uint8_t *p, uint32_t n)
{
	uint32_t c = 0xffffffffU;
	unsigned k;

	while (n--) {
		c ^= *p++;
		for (k = 0; k < 8; k++) {
			c = (c >> 1) ^ (0xedb88320U & (0U - (c & 1U)));
		}
	}
	return ~c;
}

static int sweet_fs_disk_init_once(void);
/* 0715: the rootfs file first; an invalid file falls back to the raw copy once */
static int
sweet_fs_disk_init(void)
{
	int rc = sweet_fs_disk_init_once();

	if (rc && sweet_fs_dstate < 0 && sweet_fs_src == 2 && !sweet_fs_file_bad) {
		sweet_fs_file_bad = 1;
		printf("NORD2 MODEM: fs disk blob: rootfs file unusable, falling back to raw userdata+64GiB\n");
		sweet_fs_dev_close();
		sweet_fs_src = 0;
		sweet_fs_dstate = 0;
		if (sweet_fs_dev_open() == 0) {
			rc = sweet_fs_disk_init_once();
		}
	}
	nord2_nv_src = (rc == 0 ? (unsigned)sweet_fs_src : 0U) | (sweet_fs_file_bad ? 0x100U : 0U);
	return rc;
}

static int
sweet_fs_disk_init_once(void)
{
	uint8_t *h;
	uint32_t count, tbytes, hcrc;

	if (sweet_fs_dstate) {
		return sweet_fs_dstate > 0 ? 0 : -1;
	}
	sweet_fs_dstate = -1;
	h = sweet_fs_alloc(4096);
	if (h == NULL) {
		return -1;
	}
	if (sweet_fs_dev_read(0, h, 4096)) {
		printf("NORD2 MODEM: fs disk blob: header read failed\n");
		if (++sweet_fs_dio_fails < 3) {
			sweet_fs_dstate = 0;                /* 0270: an I/O error is retried on the next need (bounded) */
		}
		goto out;
	}
	count = sweet_fs_rd(h + 12);
	tbytes = sweet_fs_rd(h + 16);
	hcrc = sweet_fs_rd(h + 32);
	sweet_fs_wr(h + 32, 0);
	if (!sweet_fs_seg_eq((const char *)h, 8, "N2NVDSK1") || sweet_fs_rd(h + 8) != 1 || count == 0 || count > 8192 ||
	    tbytes != count * 128 || sweet_fs_crc32(h, 64) != hcrc) {
		printf("NORD2 MODEM: fs disk blob: no valid header (%02x%02x%02x%02x)\n", h[0], h[1], h[2], h[3]);
		goto out;
	}
	sweet_fs_dtab_cap = (tbytes + 4095) & ~4095U;
	sweet_fs_dtab = sweet_fs_alloc(sweet_fs_dtab_cap);
	if (sweet_fs_dtab == NULL || sweet_fs_dev_read(4096, sweet_fs_dtab, sweet_fs_dtab_cap) ||
	    sweet_fs_crc32(sweet_fs_dtab, tbytes) != sweet_fs_rd(h + 20)) {
		printf("NORD2 MODEM: fs disk blob: table unreadable or corrupt\n");
		if (sweet_fs_dtab != NULL) {
			sweet_fs_free(sweet_fs_dtab, sweet_fs_dtab_cap);
			sweet_fs_dtab = NULL;
		}
		goto out;
	}
	sweet_fs_dcount = count;
	sweet_fs_dstate = 1;
	printf("NORD2 MODEM: fs disk blob: %u files (source %s)\n", count, sweet_fs_src == 2 ? "rootfs file" : "raw userdata+64GiB");
out:
	sweet_fs_free(h, 4096);
	return sweet_fs_dstate > 0 ? 0 : -1;
}

static int
sweet_fs_disk_load(const char *path, uint8_t *out, uint32_t size)
{
	uint32_t i, clen, cap, flags;
	uint64_t off;
	const uint8_t *e = NULL;
	uint8_t *tmp;
	int rc = -1;

	for (i = 0; i < sweet_fs_dcount; i++) {
		const uint8_t *t = sweet_fs_dtab + i * 128;
		unsigned k;
		for (k = 0; k < 96 && path[k] && t[k] == (uint8_t)path[k]; k++) {
		}
		if (k < 96 && path[k] == 0 && t[k] == 0) {
			e = t;
			break;
		}
	}
	if (e == NULL || sweet_fs_rd(e + 96) != size) {
		return -1;
	}
	clen = sweet_fs_rd(e + 100);
	off = (uint64_t)sweet_fs_rd(e + 104) | ((uint64_t)sweet_fs_rd(e + 108) << 32);
	flags = sweet_fs_rd(e + 116);
	if (size == 0) {
		return 0;
	}
	if (clen == 0 || (off & 4095) || (!(flags & 1) && clen != size)) {
		return -1;
	}
	cap = (clen + 4095) & ~4095U;
	tmp = sweet_fs_alloc(cap);
	if (tmp == NULL) {
		return -1;
	}
	if (sweet_fs_dev_read(off, tmp, cap) == 0 && sweet_fs_crc32(tmp, clen) == sweet_fs_rd(e + 112)) {
		if (flags & 1) {
			rc = sweet_fs_inflate(tmp, clen, out, size);
		} else {
			sweet_fs_copy(out, tmp, size);
			rc = 0;
		}
		if (rc == 0 && sweet_fs_crc32(out, size) != sweet_fs_rd(e + 120)) {
			rc = -1;
		}
	}
	sweet_fs_free(tmp, cap);
	if (sweet_fs_dlog < 16) {
		sweet_fs_dlog++;
		printf("NORD2 MODEM: fs disk blob: %s %s (%u B)\n", rc ? "FAILED" : "loaded", path, size);
	}
	return rc;
}

static int
sweet_fs_disk_read(const char *path, uint8_t *out, uint32_t size)
{
	int rc;

	if (sweet_fs_dstate < 0) {
		return -1;
	}
	if (sweet_fs_dev_open()) {                          /* 0270: lazy, opened once and kept; bounded retries in the glue */
		return -1;
	}
	rc = sweet_fs_disk_init() == 0 ? sweet_fs_disk_load(path, out, size) : -1;
	return rc;
}

/*
 * 0277ay: the disk blob is the modem's NVRAM, not only a content store for the compiled-in file list.  q10dr2 (Android's nvdata):
 * the modem read NVD_DATA/XP00_090, listed in Android's INFO_FILE and present in the blob but not in the compiled-in table, got -9
 * and raised an exception; and records with embedded contents never came from the blob at all (the IMS-record hybrid of q10dr3 was
 * served the embedded old IM* records).  Once the blob is readable (first lookup), every blob file is reconciled with the tree:
 * a known file takes the blob's size (unless the modem already loaded or wrote it), an unknown one in a known directory becomes a
 * dynamic node (as if the modem had created it; a name the modem deleted stays deleted), and sweet_fs_load prefers the blob's
 * contents over the embedded ones.
 */
static int sweet_fs_disk_synced;
static uint32_t sweet_fs_disk_find(const char *path, int *found);
static int32_t sweet_fs_create(const char *path);

static void
sweet_fs_disk_reconcile(void)
{
	uint32_t i, sized = 0, adopted = 0, failed = 0;

	for (i = 0; i < sweet_fs_dcount; i++) {
		const uint8_t *t = sweet_fs_dtab + i * 128;
		char path[97];
		uint32_t size = sweet_fs_rd(t + 96), k;
		int32_t n, j, gone = 0;

		for (k = 0; k < 96 && t[k]; k++) {
			path[k] = (char)t[k];
		}
		path[k] = 0;
		n = sweet_fs_lookup0(path);
		if (n >= 0) {
			if (!sweet_fs_isdir(n) && sweet_fs_rt[n].size != size && sweet_fs_rt[n].state == 0) {
				sweet_fs_rt[n].size = size;
				sized++;
			}
			continue;
		}
		for (j = 0; j < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn); j++) {        /* deleted by the modem: keep it deleted */
			char q[256];
			if (!(sweet_fs_rt[j].flags & 1)) {
				continue;
			}
			sweet_fs_path(j, q, sizeof(q));
			if (sweet_fs_seg_eq(q, (unsigned)strlen(q), path)) {
				gone = 1;
				break;
			}
		}
		if (gone) {
			continue;
		}
		n = sweet_fs_create(path);
		if (n < 0) {
			failed++;
			continue;
		}
		sweet_fs_rt[n].size = size;
		adopted++;
		if (adopted <= 8) {
			printf("NORD2 MODEM: fs disk blob: %s (%u B) not in the built-in tree: added\n", path, size);
		}
	}
	printf("NORD2 MODEM: fs disk blob reconciled: %u sizes taken from the blob, %u files added, %u could not be added\n", sized, adopted,
	    failed);
}

/* the size of `path` in the blob (found = 1), else found = 0 */
static uint32_t
sweet_fs_disk_find(const char *path, int *found)
{
	uint32_t i;

	*found = 0;
	for (i = 0; i < sweet_fs_dcount; i++) {
		const uint8_t *t = sweet_fs_dtab + i * 128;
		unsigned k;
		for (k = 0; k < 96 && path[k] && t[k] == (uint8_t)path[k]; k++) {
		}
		if (k < 96 && path[k] == 0 && t[k] == 0) {
			*found = 1;
			return sweet_fs_rd(t + 96);
		}
	}
	return 0;
}

static int32_t
sweet_fs_lookup(const char *path)
{
	if (!sweet_fs_disk_synced && sweet_fs_dstate >= 0 && sweet_fs_dev_open() == 0 && sweet_fs_disk_init() == 0) {
		sweet_fs_disk_synced = 1;
		sweet_fs_disk_reconcile();
	}
	return sweet_fs_lookup0(path);
}

/* make sure node n's contents are in RAM; 0 ok */
static int
sweet_fs_load(int32_t n)
{
	struct sweet_fs_rt *r = &sweet_fs_rt[n];
	uint32_t size = r->size, cap = size ? size : 1;
	uint8_t *b;

	r->lru = ++sweet_fs_lru_clock;
	if (r->state != 0 || size == 0) {
		return 0;
	}
	sweet_fs_evict(cap);
	b = sweet_fs_alloc(cap);
	if (b == NULL) {
		return -1;
	}
	int from_disk = 0;
	if (sweet_fs_disk_synced) {                                   /* 0277ay: the blob first, also for embedded files */
		char dpath[256];
		int found = 0;
		sweet_fs_path(n, dpath, sizeof(dpath));
		if (sweet_fs_disk_find(dpath, &found) == size && found && sweet_fs_disk_read(dpath, b, size) == 0) {
			from_disk = 1;
		}
	}
	if (from_disk) {
		/* contents from the blob */
	} else if (n < SWEET_NV_NODES && (sweet_nv_node[n].flags & SWEET_NV_DATA) && size == sweet_nv_node[n].size) {   /* 0277ay: not if the blob resized it */
		const struct sweet_nv_node *nv = &sweet_nv_node[n];
		if (nv->flags & SWEET_NV_DEFLATE) {
			if (sweet_fs_inflate(&sweet_nv_data[nv->off], nv->clen, b, size)) {
				sweet_fs_free(b, cap);
				return -1;
			}
		} else {
			sweet_fs_copy(b, &sweet_nv_data[nv->off], size);
		}
	} else {
		char path[256];
		sweet_fs_path(n, path, sizeof(path));
		if (sweet_fs_disk_read(path, b, size) && sweet_fs_rootfs_read(path, b, size)) {     /* 0261: disk blob, then rootfs */
			sweet_fs_free(b, cap);
			if (!(r->flags & 8)) {
				r->flags |= 8;
				printf("NORD2 MODEM: fs nocontent %s (%u B)\n", path, size);
			}
			return -1;
		}
	}
	r->buf = b;
	r->cap = cap;
	r->state = 1;
	sweet_fs_clean_bytes += cap;
	return 0;
}

/* make node n writable with room for `need` bytes; 0 ok */
static int
sweet_fs_dirty(int32_t n, uint32_t need)
{
	struct sweet_fs_rt *r = &sweet_fs_rt[n];

	if (r->size && sweet_fs_load(n)) {
		return -1;
	}
	if (r->state == 1) {                              /* clean -> dirty */
		sweet_fs_clean_bytes -= r->cap;
		sweet_fs_dirty_bytes += r->cap;
		r->state = 2;
	} else if (r->state == 0) {
		r->state = 2;
	}
	if (need > r->cap) {
		uint32_t nc = need + (need >> 2) + 64;
		uint8_t *b;
		if (sweet_fs_dirty_bytes - r->cap + nc > SWEET_FS_DIRTY_MAX) {
			return -1;
		}
		b = sweet_fs_alloc(nc);
		if (b == NULL) {
			return -1;
		}
		if (r->buf) {
			sweet_fs_copy(b, r->buf, r->size);
			sweet_fs_free(r->buf, r->cap);
		}
		sweet_fs_zero(b + r->size, nc - r->size);
		sweet_fs_dirty_bytes += nc - r->cap;
		r->buf = b;
		r->cap = nc;
	}
	return 0;
}

static void
sweet_fs_drop(int32_t n)
{
	struct sweet_fs_rt *r = &sweet_fs_rt[n];

	if (r->buf) {
		sweet_fs_free(r->buf, r->cap);
		if (r->state == 1) {
			sweet_fs_clean_bytes -= r->cap;
		} else {
			sweet_fs_dirty_bytes -= r->cap;
		}
	}
	r->buf = NULL;
	r->cap = 0;
	r->state = 0;
}

static int32_t
sweet_fs_create(const char *path)
{
	char par[256];
	int32_t p, n;
	unsigned i, last = 0, nl;

	for (i = 0; path[i] && i < sizeof(par) - 1; i++) {
		par[i] = path[i];
		if (path[i] == '/') {
			last = i;
		}
	}
	par[last] = 0;
	p = sweet_fs_lookup(par);
	if (p < 0 || !sweet_fs_isdir(p)) {
		return -1;
	}
	nl = i - last - 1;
	for (n = 0; n < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn); n++) {    /* revive a deleted entry of the same name */
		if ((sweet_fs_rt[n].flags & 1) && sweet_fs_parent(n) == p && sweet_fs_seg_eq(path + last + 1, nl, sweet_fs_name(n))) {
			sweet_fs_rt[n].flags &= (uint8_t)~1U;
			sweet_fs_rt[n].size = 0;
			return n;
		}
	}
	if (sweet_fs_ndyn >= SWEET_FS_DYN || sweet_fs_dname_used + nl + 1 > sizeof(sweet_fs_dname)) {
		return -1;
	}
	n = (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn++);
	sweet_fs_rt[n].parent = (uint16_t)p;
	sweet_fs_rt[n].flags = 2;
	sweet_fs_rt[n].name = sweet_fs_dname_used;
	sweet_fs_copy((uint8_t *)&sweet_fs_dname[sweet_fs_dname_used], (const uint8_t *)path + last + 1, nl);
	sweet_fs_dname[sweet_fs_dname_used + nl] = 0;
	sweet_fs_dname_used += nl + 1;
	sweet_fs_rt[n].size = 0;
	return n;
}

static void
sweet_fs_init(void)
{
	static int done;
	int32_t i;

	if (done) {
		return;
	}
	done = 1;
	for (i = 0; i < SWEET_NV_NODES; i++) {
		sweet_fs_rt[i].size = (sweet_nv_node[i].flags & SWEET_NV_DIR) ? 0 : sweet_nv_node[i].size;
	}
}

/* ---- reply builder (n2mdinit r_begin/r_arg/r_u32) ---- */
static void
sweet_fs_begin(const uint8_t *rq, uint32_t op)
{
	uint32_t chseq = sweet_fs_rd(rq + 8);

	sweet_fs_wr(sweet_fs_rep, sweet_fs_rd(rq));
	sweet_fs_wr(sweet_fs_rep + 4, 0);
	sweet_fs_wr(sweet_fs_rep + 8, (chseq & 0xffff0000U) | ((chseq + 1) & 0xffffU));
	sweet_fs_wr(sweet_fs_rep + 12, sweet_fs_rd(rq + 12));
	sweet_fs_wr(sweet_fs_rep + 16, op | 0xffff0000U);
	sweet_fs_wr(sweet_fs_rep + 20, 0);
	sweet_fs_rlen = 24;
}

static uint8_t *
sweet_fs_arg(const uint8_t *d, uint32_t l)
{
	uint32_t pad = ((l + 3) & ~3U) - l;
	uint8_t *at;

	if (sweet_fs_rlen + 4 + l + pad > SWEET_FS_BUF) {
		return NULL;
	}
	sweet_fs_wr(sweet_fs_rep + sweet_fs_rlen, l);
	at = sweet_fs_rep + sweet_fs_rlen + 4;
	if (d != NULL) {
		sweet_fs_copy(at, d, l);
	}
	sweet_fs_zero(at + l, pad);
	sweet_fs_rlen += 4 + l + pad;
	sweet_fs_wr(sweet_fs_rep + 20, sweet_fs_rd(sweet_fs_rep + 20) + 1);
	return at;
}

static void
sweet_fs_u32(uint32_t v)
{
	uint8_t b[4];

	sweet_fs_wr(b, v);
	sweet_fs_arg(b, 4);
}

static int
sweet_fs_halloc(void)
{
	int i;

	for (i = 1; i < SWEET_FS_NH; i++) {
		if (!sweet_fs_h[i].used) {
			sweet_fs_h[i].used = 1;
			sweet_fs_h[i].pos = 0;
			sweet_fs_h[i].wr = 0;
			return i;
		}
	}
	return -1;
}

static struct sweet_fs_h *
sweet_fs_hget(uint32_t h, int kind)
{
	if (h == 0 || h >= SWEET_FS_NH || !sweet_fs_h[h].used || (kind && sweet_fs_h[h].kind != kind)) {
		return NULL;
	}
	return &sweet_fs_h[h];
}

/* find_reply(): info {.., [3]=0xbe790001, [4]=0x5d7b, [5]=0xbe790000, [6]=0x5d7b, [7]=size}, UTF-16 name + NUL.  ok: 1 entry,
 * 0 end of list: no name, but [7] keeps the size of the last entry as the vendor daemon's reply does (n2mdinit sent 0 there). */
static void
sweet_fs_find_reply(struct sweet_fs_h *fh, int ok)
{
	uint8_t info[52], w[2 * 96];
	unsigned wn = 0, i;

	sweet_fs_zero(info, sizeof(info));
	sweet_fs_wr(info + 12, 0xbe790001U);
	sweet_fs_wr(info + 16, 0x5d7b);
	sweet_fs_wr(info + 20, 0xbe790000U);
	sweet_fs_wr(info + 24, 0x5d7b);
	if (fh->nent > 0) {
		sweet_fs_wr(info + 28, sweet_fs_size(fh->ent[fh->cur]));
	}
	if (ok) {
		const char *nm = sweet_fs_name(fh->ent[fh->cur]);
		for (i = 0; nm[i] && wn < 95; i++) {
			w[2 * wn] = (uint8_t)nm[i];
			w[2 * wn + 1] = 0;
			wn++;
		}
		w[2 * wn] = 0;
		w[2 * wn + 1] = 0;
		wn++;
	}
	sweet_fs_arg(info, sizeof(info));
	sweet_fs_arg(w, wn * 2);
}

struct sweet_fs_a {
	const uint8_t *p;
	uint32_t len;
};

/* n2mdinit parse(): op at 16, argc at 20, args from 24 */
static int
sweet_fs_parse(const uint8_t *b, uint32_t total, struct sweet_fs_a *a, int max, uint32_t *op)
{
	uint32_t argc, off = 24, i;
	int n = 0;

	if (total < 24) {
		return -1;
	}
	*op = sweet_fs_rd(b + 16);
	argc = sweet_fs_rd(b + 20);
	for (i = 0; i < argc && n < max; i++) {
		uint32_t l;
		if (off + 4 > total) {
			break;
		}
		l = sweet_fs_rd(b + off);
		off += 4;
		if (l > total - off) {
			break;
		}
		a[n].p = b + off;
		a[n].len = l;
		n++;
		off += (l + 3) & ~3U;
	}
	return n;
}

/*
 * 0277bb: an error reply has the same number of arguments as a good one. ccci_fs_get_buff (md1rom 0x90f4de2e, ccci_fs.c line 1317 =
 * 0x525) asserts that the reply's argument count equals what the caller unpacks: q10dv died on "Assert fail: ccci_fs.c 1317 0x3 0x1
 * 0x1012" (FINDFIRST answered with the status only, 3 expected) when the modem's HTTP stack (XCAP PDN up since 0277az) searched its
 * @http folders, and q10dr2 on "... 0x4 0x1 0x1022" (secure read of the missing XP00_090 answered with -9 only, 4 expected).
 */
static void
sweet_fs_find_fail(void)
{
	uint8_t z[52];

	sweet_fs_zero(z, sizeof(z));
	sweet_fs_u32((uint32_t)-6);                       /* FS_NO_MORE_FILES */
	sweet_fs_arg(z, sizeof(z));
	sweet_fs_arg(z, 0);
}

static void
sweet_fs_secure_fail(uint32_t cmd, uint32_t err)
{
	uint8_t echo[8];

	sweet_fs_wr(echo, cmd);
	sweet_fs_wr(echo + 4, err);
	sweet_fs_arg(echo, 8);
	sweet_fs_u32(err);
	sweet_fs_u32(0);
	sweet_fs_arg(NULL, 0);
}

/* one reassembled request -> reply in sweet_fs_rep/sweet_fs_rlen; returns the first reply word for the log */
static uint32_t
sweet_fs_one(const uint8_t *req, uint32_t total, char *path, unsigned pmax)
{
	struct sweet_fs_a a[8];
	uint32_t op = 0;
	int na = sweet_fs_parse(req, total, a, 8, &op);
	int32_t n;
	struct sweet_fs_h *fh;

	path[0] = 0;
	if (na < 0) {
		return 0xffffffffU;
	}
	sweet_fs_begin(req, op);
	switch (op) {
	case 0x1001: {                                    /* OPEN path flags */
		uint32_t fl = na > 1 ? sweet_fs_rd(a[1].p) : 0;
		int wr = fl >= 0x10000U, dir = (fl & 0xf00) == 0x900 && fl < 0x10000U, h;
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax)) {
			sweet_fs_u32((uint32_t)-18);
			break;
		}
		n = sweet_fs_lookup(path);
		if (n < 0 && wr) {
			n = sweet_fs_create(path);
		}
		if (n < 0 || (dir && !sweet_fs_isdir(n)) || (wr && sweet_fs_isdir(n))) {
			sweet_fs_u32((uint32_t)-18);
			break;
		}
		h = sweet_fs_halloc();
		if (h < 0) {
			sweet_fs_u32((uint32_t)-1);
			break;
		}
		sweet_fs_h[h].kind = sweet_fs_isdir(n) ? 2 : 1;
		sweet_fs_h[h].node = n;
		sweet_fs_h[h].wr = (uint8_t)wr;
		sweet_fs_u32((uint32_t)h);
		break;
	}
	case 0x1002: {                                    /* SEEK h off whence */
		int64_t base, np;
		uint32_t wh = na > 2 ? sweet_fs_rd(a[2].p) : 0;
		fh = na > 2 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (fh == NULL || fh->kind == 3) {
			sweet_fs_u32((uint32_t)-1);
			break;
		}
		base = wh == 0 ? 0 : wh == 1 ? (int64_t)fh->pos : wh == 2 ? (int64_t)sweet_fs_size(fh->node) : -1;
		np = base + (int32_t)sweet_fs_rd(a[1].p);
		if (base < 0 || np < 0) {
			sweet_fs_u32((uint32_t)-22);           /* lseek: -EINVAL */
			break;
		}
		fh->pos = (uint32_t)np;
		sweet_fs_u32((uint32_t)np);
		break;
	}
	case 0x1003: {                                    /* READ h n -> [0, nread, data] */
		uint32_t want = na > 1 ? sweet_fs_rd(a[1].p) : 0, sz, got;
		uint8_t *d;
		fh = na > 1 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (want > SWEET_FS_BUF - 64) {
			want = SWEET_FS_BUF - 64;
		}
		if (fh == NULL || fh->kind != 1 || sweet_fs_load(fh->node)) {
			sweet_fs_u32((uint32_t)-1);
			sweet_fs_u32(0);                       /* 0277bb: the full [status, n, data] shape (see sweet_fs_one) */
			sweet_fs_arg(NULL, 0);
			break;
		}
		sz = sweet_fs_rt[fh->node].size;
		got = fh->pos >= sz ? 0 : (sz - fh->pos < want ? sz - fh->pos : want);
		sweet_fs_u32(0);
		sweet_fs_u32(got);
		d = sweet_fs_arg(NULL, got);
		if (d != NULL && got) {
			sweet_fs_copy(d, sweet_fs_rt[fh->node].buf + fh->pos, got);
		}
		fh->pos += got;
		break;
	}
	case 0x1004: {                                    /* WRITE h data [n] -> [0, written] */
		uint32_t cnt = na > 2 ? sweet_fs_rd(a[2].p) : (na > 1 ? a[1].len : 0), end;
		fh = na > 1 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (na > 1 && cnt > a[1].len) {
			cnt = a[1].len;
		}
		if (fh == NULL || fh->kind != 1 || !fh->wr) {
			sweet_fs_u32((uint32_t)-1);
			sweet_fs_u32(0);                       /* 0277bb: [status, written] */
			break;
		}
		end = fh->pos + cnt;
		if (sweet_fs_dirty(fh->node, end)) {
			sweet_fs_u32((uint32_t)-1);
			sweet_fs_u32(0);
			break;
		}
		if (cnt) {
			struct sweet_fs_rt *r = &sweet_fs_rt[fh->node];
			if (fh->pos > r->size) {
				sweet_fs_zero(r->buf + r->size, fh->pos - r->size);
			}
			sweet_fs_copy(r->buf + fh->pos, a[1].p, cnt);
			if (end > r->size) {
				r->size = end;
			}
		}
		fh->pos = end;
		sweet_fs_u32(0);
		sweet_fs_u32(cnt);
		break;
	}
	case 0x1005:                                      /* CLOSE h */
		fh = na > 0 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (fh != NULL) {
			fh->used = 0;
		}
		sweet_fs_u32(0);
		break;
	case 0x1007:                                      /* MKDIR-like: the vendor daemon answers 0 for Q:\cacerts\..., -18 for S:\mdota */
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax)) {
			sweet_fs_u32((uint32_t)-18);
			break;
		}
		n = sweet_fs_lookup(path);
		if (n < 0) {
			n = sweet_fs_create(path);
			if (n >= 0) {
				sweet_fs_rt[n].flags |= 4;
			}
		}
		sweet_fs_u32(n >= 0 && sweet_fs_isdir(n) ? 0 : (uint32_t)-18);
		break;
	case 0x1009:                                      /* FILESIZE h (a directory/find handle: 4096 like fstat) */
		fh = na > 0 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (fh == NULL) {
			sweet_fs_u32((uint32_t)-1);
			sweet_fs_u32(0);                       /* 0277bb: [status, size] */
			break;
		}
		sweet_fs_u32(0);
		sweet_fs_u32(sweet_fs_size(fh->node));
		break;
	case 0x100e: {                                    /* disk info: block captured from the vendor daemon */
		uint8_t blk[84];
		sweet_fs_zero(blk, sizeof(blk));
		blk[49] = 0x02; blk[52] = 0x08; blk[56] = 0xfc; blk[57] = 0x39; blk[65] = 0x02;
		sweet_fs_u32(0);
		sweet_fs_arg(blk, sizeof(blk));
		break;
	}
	case 0x100f:                                      /* DELETE path */
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax) || (n = sweet_fs_lookup(path)) < 0 || sweet_fs_isdir(n)) {
			sweet_fs_u32((uint32_t)-9);
			break;
		}
		sweet_fs_drop(n);
		sweet_fs_rt[n].flags |= 1;
		sweet_fs_rt[n].size = 0;
		sweet_fs_u32(0);
		break;
	case 0x1010:                                      /* GETATTR path -> 0x10 dir, 0x20 file, -9 */
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax) || (n = sweet_fs_lookup(path)) < 0) {
			sweet_fs_u32((uint32_t)-9);
			break;
		}
		sweet_fs_u32(sweet_fs_isdir(n) ? 0x10 : 0x20);
		break;
	case 0x1012: {                                    /* FINDFIRST "dir\*.*" -> [h, info, name] */
		int h, pl, q, k;
		int32_t j;
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax)) {
			sweet_fs_find_fail();
			break;
		}
		for (pl = 0; path[pl]; pl++) {
		}
		for (q = pl; q > 0 && path[q - 1] != '/'; q--) {
		}
		for (k = q; k < pl; k++) {
			if (path[k] == '*' || path[k] == '?') {
				path[q > 1 ? q - 1 : 1] = 0;
				break;
			}
		}
		n = sweet_fs_lookup(path);
		if (n < 0 || !sweet_fs_isdir(n) || (h = sweet_fs_halloc()) < 0) {
			sweet_fs_find_fail();
			break;
		}
		fh = &sweet_fs_h[h];
		fh->kind = 3;
		fh->node = n;
		fh->nent = 0;
		fh->cur = 0;
		for (j = n + 1; j < (int32_t)(SWEET_NV_NODES + sweet_fs_ndyn) && fh->nent < SWEET_FS_MAXENT; j++) {
			if (sweet_fs_parent(j) == n && sweet_fs_alive(j)) {
				fh->ent[fh->nent++] = (int16_t)j;
			}
		}
		if (fh->nent == 0) {
			fh->used = 0;
			sweet_fs_find_fail();
			break;
		}
		sweet_fs_u32((uint32_t)h);
		sweet_fs_find_reply(fh, 1);
		break;
	}
	case 0x1013: {                                    /* FINDNEXT h -> [status, info, name] */
		uint8_t z[52];
		fh = na > 0 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 3) : NULL;
		if (fh == NULL) {
			sweet_fs_zero(z, sizeof(z));
			sweet_fs_u32((uint32_t)-6);
			sweet_fs_arg(z, sizeof(z));
			sweet_fs_arg(z, 0);
			break;
		}
		fh->cur++;
		if (fh->cur >= fh->nent) {
			fh->cur = (int16_t)(fh->nent - 1);
			sweet_fs_u32((uint32_t)-6);
			sweet_fs_find_reply(fh, 0);
			break;
		}
		sweet_fs_u32(0);
		sweet_fs_find_reply(fh, 1);
		break;
	}
	case 0x1014:                                      /* FINDCLOSE h */
		fh = na > 0 ? sweet_fs_hget(sweet_fs_rd(a[0].p), 0) : NULL;
		if (fh != NULL) {
			fh->used = 0;
		}
		sweet_fs_u32(0);
		break;
	case 0x101a:
		sweet_fs_u32(na > 0 && sweet_fs_rd(a[0].p) == 4 ? 0x5a : (uint32_t)-2);
		break;
	case 0x101c:
		sweet_fs_u32(0);
		break;
	case 0x1022: {                                    /* secure read: path + 40-byte descriptor (word0 cmd, 4 buf, 5 offset, 8 length) */
		uint32_t cmd, want, skip, bufaddr, sz, got;
		uint8_t echo[8], *d;
		if (na < 2 || a[1].len < 40 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax) || (n = sweet_fs_lookup(path)) < 0) {
			sweet_fs_secure_fail(na > 1 && a[1].len >= 4 ? sweet_fs_rd(a[1].p) : 0, (uint32_t)-9);
			break;
		}
		cmd = sweet_fs_rd(a[1].p);
		want = sweet_fs_rd(a[1].p + 32);
		skip = sweet_fs_rd(a[1].p + 20);
		bufaddr = sweet_fs_rd(a[1].p + 16);
		if (want > SWEET_FS_BUF - 128) {
			want = SWEET_FS_BUF - 128;
		}
		sz = sweet_fs_size(n);
		if (cmd != 0x13 && !sweet_fs_isdir(n) && sweet_fs_load(n)) {
			sweet_fs_secure_fail(cmd, (uint32_t)-9);
			break;
		}
		sweet_fs_wr(echo, cmd);
		sweet_fs_wr(echo + 4, 0);
		sweet_fs_arg(echo, 8);
		if (cmd == 0x13) {                        /* size query */
			sweet_fs_u32(sz);
			sweet_fs_u32(0);
			sweet_fs_arg(NULL, 0);
			break;
		}
		if (cmd == 0x19) {                        /* whole-file read from offset 0 */
			skip = 0;
		}
		got = sweet_fs_isdir(n) || skip >= sz ? 0 : (sz - skip < want ? sz - skip : want);
		sweet_fs_u32(cmd == 0x19 ? bufaddr : sz);
		sweet_fs_u32(got);
		d = sweet_fs_arg(NULL, got);
		if (d != NULL && got) {
			sweet_fs_copy(d, sweet_fs_rt[n].buf + skip, got);
		}
		break;
	}
	case 0x1025: {                                    /* STAT times -> [0, {mtime, 0, atime, 0, ctime, 0}] or [-9, zeros] */
		uint8_t t[24];
		sweet_fs_zero(t, sizeof(t));
		if (na < 1 || sweet_fs_dos2linux(a[0].p, a[0].len, path, pmax) || (n = sweet_fs_lookup(path)) < 0) {
			sweet_fs_u32((uint32_t)-9);
			sweet_fs_arg(t, sizeof(t));
			break;
		}
		sweet_fs_wr(t, SWEET_FS_TIME_M);
		sweet_fs_wr(t + 8, SWEET_FS_TIME_M);
		sweet_fs_wr(t + 16, SWEET_FS_TIME_C);
		sweet_fs_u32(0);
		sweet_fs_arg(t, sizeof(t));
		break;
	}
	default:
		sweet_fs_u32((uint32_t)-1);
		break;
	}
	return sweet_fs_rlen >= 32 ? sweet_fs_rd(sweet_fs_rep + 28) : 0;
}

/* n2mdinit r_send(): one packet, or several with data0 = 0x80000000 on all but the last, each = header + op id + body chunk */
static int
sweet_fs_reply(void)
{
	static uint8_t frag[SWEET_FS_PKT];
	uint32_t total = sweet_fs_rlen, off = 20;
	int rc = 0;

	if (total <= SWEET_FS_PKT) {
		sweet_fs_wr(sweet_fs_rep + 4, total);
		return sweet_fs_send(sweet_fs_rep, total);
	}
	while (off < total && rc == 0) {
		uint32_t cnt = total - off, more = 0;
		if (cnt > SWEET_FS_PKT - 20) {
			cnt = SWEET_FS_PKT - 20;
			more = 1;
		}
		sweet_fs_copy(frag, sweet_fs_rep, 16);
		sweet_fs_wr(frag, more ? 0x80000000U : 0);
		sweet_fs_wr(frag + 4, cnt + 20);
		sweet_fs_copy(frag + 16, sweet_fs_rep + 16, 4);
		sweet_fs_copy(frag + 20, sweet_fs_rep + off, cnt);
		rc = sweet_fs_send(frag, cnt + 20);
		off += cnt;
	}
	return rc;
}

/* one FS_RX packet (header + payload); reassembles like n2mdinit serve_fs() and answers complete requests */
static void
sweet_fs_rx(const uint8_t *pkt, uint32_t n)
{
	char path[256];
	uint32_t d0, op, r0;
	int rc;

	sweet_fs_init();
	if (n < 16) {
		return;
	}
	d0 = sweet_fs_rd(pkt);
	if (sweet_fs_acc == 0) {
		sweet_fs_copy(sweet_fs_req, pkt, 16);
	}
	if (sweet_fs_acc + n > SWEET_FS_BUF - 16) {
		sweet_fs_acc = 0;
		printf("NORD2 MODEM: fs request too large, dropped\n");
		return;
	}
	if (sweet_fs_acc == 0) {
		sweet_fs_copy(sweet_fs_req + 16, pkt + 16, n - 16);
		sweet_fs_acc = n - 16;
	} else if (n > 20) {
		sweet_fs_copy(sweet_fs_req + 16 + sweet_fs_acc, pkt + 20, n - 20);   /* continuation packets repeat the op id */
		sweet_fs_acc += n - 20;
	}
	if (d0 & 0x80000000U) {
		return;
	}
	sweet_fs_wr(sweet_fs_req, 0);
	sweet_fs_wr(sweet_fs_req + 4, sweet_fs_acc + 16);
	op = sweet_fs_acc >= 4 ? sweet_fs_rd(sweet_fs_req + 16) : 0;
	r0 = sweet_fs_one(sweet_fs_req, sweet_fs_acc + 16, path, sizeof(path));
	sweet_fs_acc = 0;
	sweet_fs_nops++;
	sweet_fs_opcnt[op >= 0x1000 && op < 0x1030 ? op - 0x1000 : 47]++;
	rc = sweet_fs_reply();
	if ((int32_t)r0 < 0 && op != 0x1010 && op != 0x1025) {
		sweet_fs_nerr++;
	}
	if (sweet_fs_nlog < 24 || rc != 0 || ((int32_t)r0 < 0 && op != 0x1010 && op != 0x1025 && sweet_fs_nerr <= 40)) {
		sweet_fs_nlog++;
		printf("NORD2 MODEM: fs#%u %x %s -> %x (%u B%s)\n", sweet_fs_nops, op, path, r0, sweet_fs_rlen, rc ? ", TX FAIL" : "");
	} else if (sweet_fs_nops % 500 == 0) {
		printf("NORD2 MODEM: fs %u ops (%u err) open %u read %u write %u sread %u, clean %u KB dirty %u KB\n", sweet_fs_nops, sweet_fs_nerr,
		    sweet_fs_opcnt[1], sweet_fs_opcnt[3], sweet_fs_opcnt[4], sweet_fs_opcnt[0x22], sweet_fs_clean_bytes >> 10, sweet_fs_dirty_bytes >> 10);
	}
}

/* inflate with a static arena (one stream at a time); libkern zlib, raw deflate (windowBits -15) */
static uint8_t sweet_fs_zarena[64 * 1024];
static uint32_t sweet_fs_zused;

static void *
sweet_fs_zalloc(void *opaque, unsigned items, unsigned size)
{
	uint32_t n = ((uint32_t)items * size + 15) & ~15U;
	void *p;

	(void)opaque;
	if (sweet_fs_zused + n > sizeof(sweet_fs_zarena)) {
		return NULL;
	}
	p = &sweet_fs_zarena[sweet_fs_zused];
	sweet_fs_zused += n;
	return p;
}

static void
sweet_fs_zfree(void *opaque, void *p)
{
	(void)opaque;
	(void)p;
}

static int
sweet_fs_inflate(const uint8_t *in, uint32_t inlen, uint8_t *out, uint32_t outlen)
{
	z_stream zs;
	int rc;

	sweet_fs_zero((uint8_t *)&zs, sizeof(zs));
	sweet_fs_zused = 0;
	zs.zalloc = sweet_fs_zalloc;
	zs.zfree = sweet_fs_zfree;
	zs.next_in = (Bytef *)(uintptr_t)in;
	zs.avail_in = inlen;
	zs.next_out = out;
	zs.avail_out = outlen;
	if (inflateInit2(&zs, -15) != Z_OK) {
		return -1;
	}
	rc = inflate(&zs, Z_FINISH);
	inflateEnd(&zs);
	return (rc == Z_STREAM_END && zs.total_out == outlen) ? 0 : -1;
}
/* SWEET_FS_CORE_END */

/*
 * p1: /dev/n2at, the userspace side of the AT channel (ttyC0 = CCCI_UART2 ch 12 TX / ch 10 RX, ring q5).  write() queues raw bytes
 * (commands end with CR; an SMS PDU ends with Ctrl-Z after the "> " prompt) that the service thread sends when no internal command is
 * in flight; read() returns the modem's lines (each ended with CR LF, the "> " prompt as is) from a 16 KB ring, blocking (select()
 * works) or EWOULDBLOCK with O_NONBLOCK.  One opener at a time (EBUSY), like ttyC0 on Linux.  Lines that answer the kernel's own
 * commands (boot script, 60 s status poll) are not passed up.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <sys/filio.h>
#include <sys/select.h>
#include <miscfs/devfs/devfs.h>
#include <kern/locks.h>
extern kern_return_t kernel_thread_start(thread_continue_t, void *, thread_t *);
extern void thread_deallocate(thread_t);

#define SWEET_N2AT_RXSZ 16384U
#define SWEET_N2AT_TXSZ 4096U
static LCK_GRP_DECLARE(sweet_n2at_grp, "n2at");
static LCK_MTX_DECLARE(sweet_n2at_mtx, &sweet_n2at_grp);
static uint8_t sweet_n2at_rxb[SWEET_N2AT_RXSZ], sweet_n2at_txb[SWEET_N2AT_TXSZ];
static uint32_t sweet_n2at_rxh, sweet_n2at_rxt, sweet_n2at_txh, sweet_n2at_txt;   /* free-running head (write) / tail (read) */
static uint32_t sweet_n2at_drops, sweet_n2at_opens;
static int sweet_n2at_isopen, sweet_n2at_major = -1, sweet_n2at_rsel_on, sweet_n2at_wsel_on;
static struct selinfo sweet_n2at_rsel, sweet_n2at_wsel;
static int sweet_svc_kick;                        /* wakes the steady service loop (write to /dev/n2at) */
static int sweet_md_dead;                         /* the MD signalled an exception: no more AT */
static int sweet_steady;                          /* steady service (after the boot script) */

/*
 * 0277 NORD2 BB: the baseband bridge (iokit/Kernel/arm/Nord2Baseband.cpp) installs these hooks from its static constructor; NULL
 * without it.  Kernel-internal AT queries (nord2_at_query) are a fourth user of the AT channel, owner 3: sent by the service thread
 * when the channel is idle (no internal, user or bridge command in flight, no /dev/n2at bytes waiting), the boot script and the
 * status poll wait for them, their response lines stay in the kernel and are never logged (they can hold IMEI/IMSI/ICCID).
 */
#include "nord2_bb_kpi.h"
const struct nord2_bb_hooks *nord2_bb_hooks;
/* 0710/0711/0712 (sim-20261115): see nord2_bb_kpi.h; the SYSCTL lines are next to the net.nord2 node below */
unsigned nord2_sim_fix = 15, nord2_sim_state, nord2_sim_apn = 1, nord2_nr = 1, nord2_nr_state;
unsigned nord2_bb_link, nord2_sim_helper;           /* 0713 */
unsigned nord2_nr_ui;                               /* 0714, default 0 (experiment) */
#define NORD2_ATQ_N    32
#define NORD2_ATQ_CMD  224                       /* 0277aa: was 64 (AT+EAPNSET="ims",2,"type=ims;..." is ~190) */
#define NORD2_ATQ_RESP 1024
#define NORD2_ATQ_MS   10000U                     /* owner 3 timeout */
#define NORD2_ATQ_LONG_MS 60000U                  /* 0277r: AT+CMGS (network round trip), ATD, ATA, AT+CHLD, ATH */
static struct {
	char cmd[NORD2_ATQ_CMD];
	nord2_at_done_t done;
	void *ctx;
	int has_pdu;                              /* 0277r: AT+CMGS with sweet_atq_pdu after the "> " prompt */
} sweet_atq[NORD2_ATQ_N];
static unsigned sweet_atq_head, sweet_atq_count;  /* sweet_n2at_mtx */
static struct {                                   /* the bridge query in flight: service thread only */
	nord2_at_done_t done;
	void *ctx;
	unsigned len;
	unsigned tmo_ms;                          /* 0277r: NORD2_ATQ_MS, or NORD2_ATQ_LONG_MS */
	char cmd[NORD2_ATQ_CMD];                  /* 0277z: for the failure line (sweet_atq_finish) */
	uint32_t t0;
	int has_pdu, pdu_pending;                 /* 0277r: owns sweet_atq_pdu; the PDU still waits for the prompt */
	char text[NORD2_ATQ_RESP];
} sweet_atq_cur;
/* 0277r: the one SMS PDU (hex, SMSC part first) of a queued or running AT+CMGS; never logged */
static char sweet_atq_pdu[NORD2_AT_PDU_MAX + 1];
static unsigned sweet_atq_pdu_len;
static int sweet_atq_pdu_busy;                    /* sweet_n2at_mtx */
static unsigned sweet_atq_sent, sweet_atq_ok, sweet_atq_err, sweet_atq_tmo, sweet_atq_full;
static void sweet_atq_add(const char *l, unsigned n);
static void sweet_atq_finish(int final);
static void sweet_at_send_pdu(void);

static void
sweet_bb_event(int what)
{
	if (nord2_bb_hooks != NULL && nord2_bb_hooks->event != NULL) {
		nord2_bb_hooks->event(what);
	}
}

/* service thread: pass one received line (crlf) or the raw prompt up to the reader; dropped (and counted) if it does not fit */
static void
sweet_n2at_deliver(const char *s, uint32_t n, int crlf)
{
	uint32_t i, need = n + (crlf ? 2 : 0);

	lck_mtx_lock(&sweet_n2at_mtx);
	if (sweet_n2at_isopen) {
		if (SWEET_N2AT_RXSZ - (sweet_n2at_rxh - sweet_n2at_rxt) < need) {
			sweet_n2at_drops++;
		} else {
			for (i = 0; i < n; i++) {
				sweet_n2at_rxb[sweet_n2at_rxh++ % SWEET_N2AT_RXSZ] = (uint8_t)s[i];
			}
			if (crlf) {
				sweet_n2at_rxb[sweet_n2at_rxh++ % SWEET_N2AT_RXSZ] = '\r';
				sweet_n2at_rxb[sweet_n2at_rxh++ % SWEET_N2AT_RXSZ] = '\n';
			}
			wakeup(&sweet_n2at_rxh);
			if (sweet_n2at_rsel_on) {
				sweet_n2at_rsel_on = 0;
				selwakeup(&sweet_n2at_rsel);
			}
		}
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
}

static void n2net_at_line(const char *l);          /* N2NET: data-call info from the AT replies */
static void sweet_net_status_fmt(char *b, unsigned n);   /* N2NET: net part of the 60 s status line */

/*
 * 0262: in-kernel AT client on the modem's ttyC0 port.  port_cfg.c md1_ccci_ports[]: {CCCI_UART2_TX, CCCI_UART2_RX, DATA_AT_CMD_Q,
 * DATA_AT_CMD_Q, 0xFF, 0xFF, MD1_NORMAL_HIF, PORT_F_WITH_CHAR_NODE, &char_port_ops, 3, "ttyC0"} with DATA_AT_CMD_Q = 5 for
 * MD_GENERATION > 6292: TX channel 12 / RX channel 10 on CCIF ring q5 (CCCI_UART1 6/8 is the MD logger control port "ccci_md_log_ctrl",
 * not AT).  TX like port_dev_write() for a port without PORT_F_USER_HEADER: {data0 0, data1 16 + len, ch 12, reserved 0} + text; Linux
 * refuses it before READY.  RX (ch 10) payloads are joined into lines and logged as "NORD2 AT< ...", digit runs of 8+ masked.
 */
#ifndef SWEET_RIL_BOOT
#define SWEET_RIL_BOOT 1                          /* 0277au: the RIL's pre-radio commands + AT+EFUN=1 (see the script) */
#endif
#ifndef SWEET_DATA_EAPNACT
#define SWEET_DATA_EAPNACT 1                      /* 0277at: data PDN the stock-ROM way (bridge), no AT+CGDCONT/CGACT for cid 1 */
#endif
static const struct {
	const char *cmd;
	unsigned wait_ms;                         /* delay before the next command */
} sweet_at_script[] = {
	/* 0277m: the radio function (and with it the SIM card) first: before it the modem answers +CPIN? with an error, and the
	 * baseband bridge holds ABM's boot until the card is read (Nord2Baseband.cpp bb_hold_*) */
	/* 0277ar: as mtkfusionrild does on the stock ROM (ANDROID-IMS-LOG at-boot2.txt, IMS registers there) before the radio is on:
	 * the IMS bearer handshake armed, PS bearer reports, the IWLAN RAT list mode 2. The APN table (inside AT+EAPNLOCK=1/0) and IMS
	 * on (AT+EIMSCFG) follow in the bridge's IMS setup after the SIM is ready, in the RIL's order (Nord2Baseband.cpp bb_ims_cmds). */
	{ "ATE0\r", 1000 },
#if SWEET_RIL_BOOT
	/* 0277au: mtkfusionrild's commands before the radio is on, in its order (ANDROID-IMS-LOG at-boot2.txt, slot 0, systematic diff
	 * ims/atdiff.py): URC / IMS / data-assistant configuration incl. AT+EBOOT=0, AT+EDSBP=2 (dynamic SBP: the data access manager
	 * refuses PDNs while a D-SBP is "on going", d2am_is_pdn_allowed), AT+EPOC, AT+EFUN=0, and the radio with AT+EFUN=1 as the RIL
	 * (not AT+CFUN=1). Left out: queries, 5G/C2K/STK/USSD extras, AT+ECFGSET="mtk_ct_volte_support" (an AT+ECFGGET of that key
	 * wedged the AT channel in q10cu), AT+ERAT/AT+EGRAT added in 0277av (see below). */
	{ "AT+ECFGSET=\"disable_cs_call\",\"0\"\r", 250 },
	{ "AT+EINFO=1074\r", 250 },
	{ "AT+EGMC=1,\"stk_call_over_ims\",1\r", 250 },
	{ "AT+CIREG=2\r", 250 },
	{ "AT+CMOD=0\r", 250 },
	{ "AT+ECFGSET=\"report_eimsreginfo_urc\",\"1\"\r", 250 },
	{ "AT+EAIC=2\r", 250 },
	{ "AT+EVOCD=1\r", 250 },
	{ "AT+ECCP=1\r", 250 },
	{ "AT+EECCFC=1\r", 250 },
	{ "AT+CIREP=1\r", 250 },
	{ "AT+EMDRSRA=1\r", 250 },
	{ "AT+EIAREG=1\r", 250 },
	{ "AT+ECCAUSE=1\r", 250 },
	{ "AT+EREG=3\r", 250 },
	{ "AT+EIMSPDN= \"onoff\", 1\r", 250 },
	{ "AT+EGREG=3,1\r", 250 },
	{ "AT+EEPDG=1\r", 250 },
	{ "AT+PSBEARER=1\r", 250 },
	{ "AT+EIWLPLEN =2\r", 250 },
	{ "AT+ECFGSET=\"disable_cs_ss\",\"0\"\r", 250 },
	{ "AT+EAPC=1,1,0\r", 250 },
	{ "AT+ESIMS=1\r", 250 },
	{ "AT+EIMSESS=1\r", 250 },
	{ "AT+ECFGSET=\"disable_sms\", \"0\"\r", 250 },
	{ "AT+CSCON=3\r", 250 },
	{ "AT+ECAMPUI=1\r", 250 },
	{ "AT+ECAPABILITY=1\r", 250 },
	{ "AT+EDSDA=1\r", 250 },
	{ "AT+EBOOT=0\r", 250 },
	{ "AT+EDSBP=2\r", 250 },
	{ "AT+EPOC\r", 250 },
	{ "AT+EFUN=0\r", 250 },
	{ "AT+ESLOTSINFO=1\r", 250 },
	/* 0277av (q10do: with AT+EFUN=1 and no RAT mode the modem registered on 3G only, +EREG act 4096/8192, CGACT 0,0 = no EPS
	 * default bearer, AT+EAPNACT default +CME ERROR 5873): the RIL's RAT mode, AT+EGRAT=22 before and AT+ERAT=22,128 after EFUN=1 */
	{ "AT+EGRAT=22\r", 250 },
	{ "AT+EFUN=1\r", 3000 },
	{ "AT+EIWLPLEN =2\r", 250 },                     /* 0277aw: the RIL sends it again right after EFUN=1 */
	{ "AT+ERAT=22,128\r", 500 }, { "AT+EANBR=1\r", 500 },
#else
	{ "AT+EIMSPDN= \"onoff\", 1\r", 500 }, { "AT+PSBEARER=1\r", 500 }, { "AT+EIWLPLEN =2\r", 500 },
	{ "AT+CFUN=1\r", 3000 },
#endif
	{ "AT+CMEE=2\r", 1000 }, { "ATI\r", 1000 }, { "AT+CEREG=2\r", 1000 },
	{ "AT+CREG=2\r", 1000 }, { "AT+CPIN?\r", 1000 }, { "AT+COPS=0\r", 3000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
#if SWEET_DATA_EAPNACT
	/* 0277at: no AP-side cid 1 any more: the internet PDN is the modem's (DAM) default PDN, brought up the stock-ROM way by the
	 * bridge (AT+EDRETRY + AT+EAPNACT=1,"internet.it","default",... on "+EIWLPL: "setup","default"" / Start Network) */
	{ "AT+COPS=3,0\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CGDCONT?\r", 2000 }, { "AT+CGACT?\r", 1000 },
#else
	{ "AT+COPS=3,0\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CGDCONT?\r", 2000 }, { "AT+CGDCONT=1,\"IP\",\"internet.it\"\r", 2000 },
	{ "AT+CGACT=1,1\r", 8000 }, { "AT+CGACT?\r", 1000 }, { "AT+CGPADDR=1\r", 1000 }, { "AT+CGCONTRDP=1\r", 3000 },
#endif
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
	{ "AT+CEREG?\r", 1000 }, { "AT+CREG?\r", 1000 }, { "AT+COPS?\r", 1000 }, { "AT+CSQ\r", 1000 }, { "AT+CGATT?\r", 11000 },
};
#define SWEET_AT_N (sizeof(sweet_at_script) / sizeof(sweet_at_script[0]))
#define SWEET_AT_MAXLINES 700
static unsigned sweet_at_step, sweet_at_next_ms, sweet_at_lines, sweet_at_len, sweet_at_rxbytes;
static int sweet_at_armed;
static char sweet_at_line[640];                   /* 0277r: was 200; an SMS PDU line (+CMT, +CMGR) is up to 2 x 176 + 2 characters */
static int sweet_at_urc_pdu;                      /* 0277r: the next line is the PDU of a +CMT/+CDS/+CBM URC */
static int sweet_at_dialcmd;                      /* 0277r: ATD/ATA in flight: NO CARRIER/BUSY/NO ANSWER end it */
/*
 * p1: one AT channel, three users: the boot script, the 60 s status poll (steady service) and /dev/n2at.  A command is in flight from
 * its send until a final result code (OK, ERROR, +CME ERROR, +CMS ERROR, NO CARRIER, BUSY, NO ANSWER, NO DIALTONE) or a timeout
 * (internal 30 s, user 120 s).  Script/status commands are not sent while a user command is in flight and user bytes are held while an
 * internal one is.  While an internal command is in flight its response lines (no '+' prefix, or its own prefix such as "+CSQ:") stay
 * in the kernel; everything else (URCs, user responses) goes to the /dev/n2at reader.
 */
static int sweet_at_log = 1;                      /* log AT traffic (bring-up); 0 in the steady service */
static int sweet_at_owner;                        /* in flight: 0 none, 1 internal, 2 user, 3 bridge query (0277) */
static unsigned sweet_at_owner_ms, sweet_at_user_cmds, sweet_at_user_bytes;
static char sweet_at_pfx[20];                     /* response prefix of the internal command in flight, "" for ATI/ATE0 */
static int sweet_st_step = -1;                    /* status poll: -1 idle, 0..3 */
static unsigned sweet_st_next_ms;
static int sweet_st_reg, sweet_st_act, sweet_st_mode, sweet_st_oact, sweet_st_rssi, sweet_st_ber;
static char sweet_st_oper[24];

static int
sweet_at_is_final(const char *l)
{
	return !strcmp(l, "OK") || !strcmp(l, "ERROR") || !strncmp(l, "+CME ERROR", 10) || !strncmp(l, "+CMS ERROR", 10) ||
	       !strcmp(l, "NO CARRIER") || !strcmp(l, "BUSY") || !strcmp(l, "NO ANSWER") || !strcmp(l, "NO DIALTONE");
}

/* response prefix of an AT command: "AT+CEREG?" -> "+CEREG:", "ATI" -> "" */
static void
sweet_at_set_pfx(const char *cmd)
{
	unsigned i = 0;

	sweet_at_pfx[0] = 0;
	if (cmd[0] != 'A' || cmd[1] != 'T' || cmd[2] != '+') {
		return;
	}
	sweet_at_pfx[i++] = '+';
	for (cmd += 3; i < sizeof(sweet_at_pfx) - 2 && ((*cmd >= 'A' && *cmd <= 'Z') || (*cmd >= '0' && *cmd <= '9')); cmd++) {
		sweet_at_pfx[i++] = *cmd;
	}
	sweet_at_pfx[i++] = ':';
	sweet_at_pfx[i] = 0;
}

/* fields of "a,b,"c",d" after the prefix: numbers -> v[] (-1 empty, -2 quoted); the first quoted field -> q (printable only) */
static int
sweet_at_fields(const char *s, int *v, int max, char *q, unsigned qmax)
{
	int n = 0, gotq = 0;

	q[0] = 0;
	while (n < max) {
		int val = -1, neg = 0;
		while (*s == ' ') {
			s++;
		}
		if (*s == '"') {
			unsigned k = 0;
			for (s++; *s && *s != '"'; s++) {
				if (!gotq && k + 1 < qmax && *s >= 32 && *s < 127) {
					q[k++] = *s;
				}
			}
			if (!gotq) {
				q[k] = 0;
				gotq = 1;
			}
			if (*s == '"') {
				s++;
			}
			val = -2;
		} else {
			if (*s == '-') {
				neg = 1;
				s++;
			}
			if (*s >= '0' && *s <= '9') {
				for (val = 0; *s >= '0' && *s <= '9'; s++) {
					val = val * 10 + (*s - '0');
				}
				val = neg ? -val : val;
			}
		}
		v[n++] = val;
		while (*s && *s != ',') {
			s++;
		}
		if (*s != ',') {
			break;
		}
		s++;
	}
	return n;
}

/* answers of the status poll */
static void
sweet_st_parse(const char *l)
{
	int v[6], n;
	char q[24];

	if (sweet_st_step < 0) {
		return;
	}
	if (!strncmp(l, "+CEREG:", 7)) {
		n = sweet_at_fields(l + 7, v, 6, q, sizeof(q));
		sweet_st_reg = n >= 2 ? v[1] : -1;
		sweet_st_act = n >= 5 ? v[4] : -1;
	} else if (!strncmp(l, "+COPS:", 6)) {
		n = sweet_at_fields(l + 6, v, 6, sweet_st_oper, sizeof(sweet_st_oper));
		sweet_st_mode = n >= 1 ? v[0] : -1;
		sweet_st_oact = n >= 4 ? v[3] : -1;
	} else if (!strncmp(l, "+CSQ:", 5)) {
		n = sweet_at_fields(l + 5, v, 6, q, sizeof(q));
		sweet_st_rssi = n >= 1 ? v[0] : -1;
		sweet_st_ber = n >= 2 ? v[1] : -1;
	}
}

/* log one received line; any run of 8 or more digits (IMEI/IMSI/ICCID-like) is replaced by '#' */
static void
sweet_at_emit(void)
{
	unsigned i, j;
	int internal = 0, final, bbline = 0, pdu_line = 0, callword, urc_only;

	if (sweet_at_len == 0) {
		return;
	}
	sweet_at_line[sweet_at_len] = 0;
	n2net_at_line(sweet_at_line);                      /* N2NET: +EIF / +CGPADDR / +CGCONTRDP, every line, before routing/masking */
	final = sweet_at_is_final(sweet_at_line);                       /* p1: route the raw line */
	/*
	 * 0277r (calls and SMS for the baseband bridge): lines that are always unsolicited even while an internal or bridge command is in
	 * flight: the PDU line after a +CMT/+CDS/+CBM URC (it has no '+' and was taken for the command's response), RING, and the call
	 * results NO CARRIER/BUSY/NO ANSWER unless the command in flight is ATD/ATA (a call ended by the network is not the final result
	 * of an AT+CLCC or AT+CSQ that happened to be running).
	 */
	if (sweet_at_urc_pdu) {
		sweet_at_urc_pdu = 0;
		pdu_line = 1;
	}
	callword = !strcmp(sweet_at_line, "NO CARRIER") || !strcmp(sweet_at_line, "BUSY") || !strcmp(sweet_at_line, "NO ANSWER");
	if (callword && (sweet_at_owner == 1 || sweet_at_owner == 3) && !sweet_at_dialcmd) {
		final = 0;
	}
	urc_only = pdu_line || (callword && !final) || !strcmp(sweet_at_line, "RING");
	if (!urc_only && (sweet_at_owner == 1 || sweet_at_owner == 3) && (sweet_at_line[0] != '+' || final ||
	    (sweet_at_pfx[0] && !strncmp(sweet_at_line, sweet_at_pfx, strlen(sweet_at_pfx))))) {
		internal = 1;
		if (sweet_at_owner == 1) {
			sweet_st_parse(sweet_at_line);
		} else {
			bbline = 1;                            /* 0277: a bridge query's line: kept, never logged */
			if (sweet_at_line[0] != 'A' || sweet_at_line[1] != 'T') {      /* echo (before ATE0) dropped */
				sweet_atq_add(sweet_at_line, sweet_at_len);
			}
		}
	} else {
		sweet_n2at_deliver(sweet_at_line, sweet_at_len, 1);
		if (nord2_bb_hooks != NULL && nord2_bb_hooks->urc != NULL) {
			nord2_bb_hooks->urc(sweet_at_line);    /* 0277: the bridge watches +CEREG/+CREG/+CPIN... prefixes only */
		}
		if (!pdu_line && (!strncmp(sweet_at_line, "+CMT:", 5) || !strncmp(sweet_at_line, "+CDS:", 5) ||
		    !strncmp(sweet_at_line, "+CBM:", 5))) {
			sweet_at_urc_pdu = 1;              /* 0277r: its PDU line follows */
		}
	}
	if (final && sweet_at_owner != 0) {
		int was = sweet_at_owner;
		sweet_at_owner = 0;
		sweet_at_pfx[0] = 0;
		if (was == 3) {
			sweet_atq_finish(strcmp(sweet_at_line, "OK") ? NORD2_AT_FINAL_ERROR : NORD2_AT_FINAL_OK);
		}
	}
	(void)internal;
	if (!sweet_at_log || bbline) {
		sweet_at_len = 0;
		return;
	}
	if (pdu_line) {                                    /* 0277r: an SMS body is never logged */
		if (sweet_at_lines < SWEET_AT_MAXLINES) {
			sweet_at_lines++;
			printf("NORD2 AT< (SMS PDU, %u hex characters, not logged)\n", sweet_at_len);
		}
		sweet_at_len = 0;
		return;
	}
	for (i = 0; i < sweet_at_len;) {
		if (sweet_at_line[i] >= '0' && sweet_at_line[i] <= '9') {
			for (j = i; j < sweet_at_len && sweet_at_line[j] >= '0' && sweet_at_line[j] <= '9'; j++) {
			}
			if (j - i >= 8) {
				for (; i < j; i++) {
					sweet_at_line[i] = '#';
				}
			}
			i = j;
		} else {
			i++;
		}
	}
	if (sweet_at_lines < SWEET_AT_MAXLINES) {
		sweet_at_lines++;
		printf("NORD2 AT< %s\n", sweet_at_line);
	} else if (sweet_at_lines == SWEET_AT_MAXLINES) {
		sweet_at_lines++;
		printf("NORD2 AT< (line limit reached, further lines not logged)\n");
	}
	sweet_at_len = 0;
}

static void
sweet_at_rx(const uint8_t *d, uint32_t n)
{
	uint32_t i;

	sweet_at_rxbytes += n;
	for (i = 0; i < n; i++) {
		uint8_t c = d[i];
		if (c == '\r' || c == '\n' || c == 0) {
			sweet_at_emit();
			continue;
		}
		sweet_at_line[sweet_at_len++] = (c >= 32 && c < 127) ? (char)c : '?';
		if (sweet_at_len >= sizeof(sweet_at_line) - 1) {
			sweet_at_emit();
		}
	}
	if (sweet_at_len > 0 && sweet_at_len <= 2 && sweet_at_line[0] == '>') {   /* p1: the SMS "> " prompt has no line end */
		if (sweet_at_owner == 3 && sweet_atq_cur.pdu_pending) {
			sweet_atq_cur.pdu_pending = 0;     /* 0277r: the bridge's AT+CMGS: its PDU and Ctrl-Z now */
			sweet_at_send_pdu();
		} else {
			sweet_n2at_deliver(sweet_at_line, sweet_at_len, 0);
		}
		sweet_at_len = 0;
	}
}

static void
sweet_at_send_as(const char *cmd, int owner)
{
	uint8_t pkt[16 + 256];                          /* 0277aa: bridge queries up to NORD2_ATQ_CMD */
	char shown[64];
	uint32_t n, k = 0, quotes;                      /* 0710: quotes */
	int rc;

	for (n = 0; cmd[n] && n < 256; n++) {
		pkt[16 + n] = (uint8_t)cmd[n];
		if (cmd[n] != '\r' && k < sizeof(shown) - 1) {
			shown[k++] = cmd[n];
		}
	}
	shown[k] = 0;
	/* 0277r: ATD/ATA end at NO CARRIER/BUSY/NO ANSWER; a dialled number or DTMF digits are logged as their length + the last 2 */
	sweet_at_dialcmd = cmd[0] == 'A' && cmd[1] == 'T' && (cmd[2] == 'D' || cmd[2] == 'A');
	if (cmd[0] == 'A' && cmd[1] == 'T' && (cmd[2] == 'D' || !strncmp(cmd, "AT+VTS=", 7) || !strncmp(cmd, "AT+EVTS=0", 9))) {
		unsigned d = 0;
		char t1 = '-', t2 = '-';
		for (k = 0; k < sizeof(shown) - 1 && shown[k]; k++) {
			if ((shown[k] >= '0' && shown[k] <= '9') || shown[k] == '*' || shown[k] == '#' || shown[k] == '+') {
				d++;
				t1 = t2;
				t2 = shown[k];
			}
		}
		snprintf(shown, sizeof(shown), "%.3s<%u digits ..%c%c>", cmd, d, t1, t2);
	}
	/* 0710: commands that carry a PIN / PUK / password are logged up to their '=' (UIM Verify/Unblock/Change PIN, Set PIN Protection) */
	for (k = 0, quotes = 0; cmd[k]; k++) {
		quotes += cmd[k] == '"';
	}
	if (!strncmp(cmd, "AT+CPIN=", 8) || !strncmp(cmd, "AT+CPWD=", 8) || !strncmp(cmd, "AT+EPIN2=", 9) ||
	    (!strncmp(cmd, "AT+CLCK=", 8) && quotes > 2)) {
		unsigned eq = 0;
		while (cmd[eq] && cmd[eq] != '=' && eq < 12) {
			eq++;
		}
		snprintf(shown, sizeof(shown), "%.*s=<not logged>", (int)eq, cmd);
	}
	sweet_wr32(pkt, 0);
	sweet_wr32(pkt + 4, 16 + n);
	sweet_wr32(pkt + 8, 12);                            /* CCCI_UART2_TX */
	sweet_wr32(pkt + 12, 0);
	rc = sweet_ring_tx(5, pkt, 16 + n);                 /* DATA_AT_CMD_Q */
	sweet_at_owner = owner;                             /* p1: internal command in flight; 0277: or a bridge query */
	sweet_at_owner_ms = sweet_tick_ms;
	sweet_at_set_pfx(cmd);
	if (sweet_at_log) {
		printf("NORD2 AT> %s%s (t=%u ms, tx %d)\n", owner == 3 ? "[bb] " : "", shown, sweet_tick_ms, rc);
	}
}

static void
sweet_at_send(const char *cmd)
{
	sweet_at_send_as(cmd, 1);
}

/* ---- 0277 NORD2 BB: kernel-internal AT queries (nord2_bb_kpi.h) ---- */
static int
sweet_atq_put2(const char *cmd, const char *pdu, nord2_at_done_t done, void *ctx, int urgent);
static int
sweet_atq_put(const char *cmd, const char *pdu, nord2_at_done_t done, void *ctx)
{
	return sweet_atq_put2(cmd, pdu, done, ctx, 0);
}

/* 0283: urgent = at the head of the queue (a call's answer/end must not wait behind a setup burst: q10ec "ATA -> +CME ERROR: 100"
 * after 8 setup commands, the call already released) */
static int
sweet_atq_put2(const char *cmd, const char *pdu, nord2_at_done_t done, void *ctx, int urgent)
{
	unsigned n, p = 0;
	int err = 0;

	if (cmd == NULL || done == NULL || cmd[0] != 'A' || cmd[1] != 'T') {
		return EINVAL;
	}
	for (n = 0; cmd[n]; n++) {
		if (n >= NORD2_ATQ_CMD - 3 || cmd[n] == '\r' || cmd[n] == '\n') {
			return EINVAL;
		}
	}
	if (pdu != NULL) {                                          /* 0277r: hex only, even length */
		for (p = 0; pdu[p]; p++) {
			char c = pdu[p];
			if (p >= NORD2_AT_PDU_MAX || !((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
				return EINVAL;
			}
		}
		if (p < 2 || (p & 1)) {
			return EINVAL;
		}
	}
	if (sweet_md_dead) {
		return ENXIO;
	}
	lck_mtx_lock(&sweet_n2at_mtx);
	if (sweet_atq_count == NORD2_ATQ_N || (pdu != NULL && sweet_atq_pdu_busy)) {
		sweet_atq_full++;
		err = EBUSY;
	} else {
		unsigned slot;
		if (urgent && pdu == NULL) {
			sweet_atq_head = (sweet_atq_head + NORD2_ATQ_N - 1) % NORD2_ATQ_N;
			sweet_atq_count++;
			slot = sweet_atq_head;
		} else {
			slot = (sweet_atq_head + sweet_atq_count++) % NORD2_ATQ_N;
		}
		memcpy(sweet_atq[slot].cmd, cmd, n);
		sweet_atq[slot].cmd[n] = '\r';
		sweet_atq[slot].cmd[n + 1] = 0;
		sweet_atq[slot].done = done;
		sweet_atq[slot].ctx = ctx;
		sweet_atq[slot].has_pdu = pdu != NULL;
		if (pdu != NULL) {
			memcpy(sweet_atq_pdu, pdu, p);
			sweet_atq_pdu[p] = 0;
			sweet_atq_pdu_len = p;
			sweet_atq_pdu_busy = 1;
		}
		sweet_svc_kick = 1;                                 /* the steady loop sends it at once */
		wakeup(&sweet_svc_kick);
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	return err;
}

int
nord2_at_query(const char *cmd, nord2_at_done_t done, void *ctx)
{
	return sweet_atq_put(cmd, NULL, done, ctx);
}

int
nord2_at_query_urgent(const char *cmd, nord2_at_done_t done, void *ctx)
{
	return sweet_atq_put2(cmd, NULL, done, ctx, 1);
}

/* 0277r: AT+CMGS in PDU mode (nord2_bb_kpi.h) */
int
nord2_at_query_pdu(const char *cmd, const char *pdu, nord2_at_done_t done, void *ctx)
{
	if (pdu == NULL) {
		return EINVAL;
	}
	return sweet_atq_put(cmd, pdu, done, ctx);
}

static void
sweet_atq_pdu_release(void)
{
	lck_mtx_lock(&sweet_n2at_mtx);
	sweet_atq_pdu_busy = 0;
	lck_mtx_unlock(&sweet_n2at_mtx);
}

/* 0277r: the PDU of the bridge's AT+CMGS after the "> " prompt, then Ctrl-Z (service thread); the network's answer starts now */
static void
sweet_at_send_pdu(void)
{
	static uint8_t pkt[16 + NORD2_AT_PDU_MAX + 1];
	uint32_t n = sweet_atq_pdu_len;
	int rc;

	if (n > NORD2_AT_PDU_MAX) {
		return;
	}
	memcpy(pkt + 16, sweet_atq_pdu, n);
	pkt[16 + n++] = 0x1a;
	sweet_wr32(pkt, 0);
	sweet_wr32(pkt + 4, 16 + n);
	sweet_wr32(pkt + 8, 12);                            /* CCCI_UART2_TX */
	sweet_wr32(pkt + 12, 0);
	rc = sweet_ring_tx(5, pkt, 16 + n);                 /* DATA_AT_CMD_Q */
	sweet_at_owner_ms = sweet_tick_ms;
	if (sweet_at_log || rc != 0) {
		printf("NORD2 AT> [bb] (SMS PDU, %u hex characters, not logged) + Ctrl-Z (tx %d)\n", n - 1, rc);
	}
}

/* 0277w: nord2_bb_kpi.h; the baseband bridge restarts InCallService after calls (Nord2Baseband.cpp bb_ics_tick) */
#include <sys/proc_internal.h>
#include <sys/signal.h>
struct sweet_kill_named {
	const char *name;
	int n;
	int pids[8];
};

static int
sweet_kill_named_filter(proc_t p, void *arg)
{
	const struct sweet_kill_named *k = arg;
	const char *nm = proc_best_name(p);
	return nm != NULL && strcmp(nm, k->name) == 0;
}

static int
sweet_kill_named_callout(proc_t p, void *arg)
{
	struct sweet_kill_named *k = arg;
	if (k->n < 8) {
		k->pids[k->n++] = proc_pid(p);
	}
	return PROC_RETURNED;
}

int
nord2_proc_kill_named(const char *name)
{
	struct sweet_kill_named k = { name, 0, { 0 } };

	if (name == NULL || name[0] == 0) {
		return 0;
	}
	proc_iterate(PROC_ALLPROCLIST, sweet_kill_named_callout, &k, sweet_kill_named_filter, &k);
	for (int i = 0; i < k.n; i++) {
		if (k.pids[i] > 1) {
			proc_signal(k.pids[i], SIGKILL);
		}
	}
	return k.n;
}

unsigned
nord2_modem_state(void)
{
	return (sweet_md_ready ? NORD2_MODEM_READY : 0u) | (sweet_steady ? NORD2_MODEM_STEADY : 0u) | (sweet_md_dead ? NORD2_MODEM_DEAD : 0u);
}

/* a response line of the bridge query in flight (service thread) */
static void
sweet_atq_add(const char *l, unsigned n)
{
	if (sweet_atq_cur.len + n + 1 >= NORD2_ATQ_RESP) {
		return;                                             /* truncated: the parser sees fewer lines */
	}
	memcpy(sweet_atq_cur.text + sweet_atq_cur.len, l, n);
	sweet_atq_cur.len += n;
	sweet_atq_cur.text[sweet_atq_cur.len++] = '\n';
	sweet_atq_cur.text[sweet_atq_cur.len] = 0;
}

/*
 * 0277z: a failed bridge query is always logged with its command and final line ("NORD2 AT! [bb] AT+CGACT=1,2 -> +CME ERROR: 30
 * (1200 ms)"), at most 400 a boot. Commands that can carry a number, a PIN or a password (ATD, AT+VTS/EVTS, AT+CPIN/CLCK/CPWD,
 * anything not listed) are shown only up to their '='; the final line of an error never carries an identity.
 */
static void
sweet_atq_fail_line(int final)
{
	static const char *const full[] = { "AT+CG", "AT+EAPN", "AT+EPDN", "AT+CEER", "AT+CMGF", "AT+CSMS", "AT+CNMI", "AT+CSCA?",
		                            "AT+CLIP", "AT+CRC", "AT+CCWA", "AT+ECPI", "AT+CNMA", "AT+CMGS", "AT+CLCC", "AT+CHLD",
		                            "ATA", "ATH", "AT+CRSM", "AT+COPS", "AT+CREG", "AT+CEREG", "AT+CSQ", "AT+CESQ", "AT+CFUN" };
	static unsigned lines;
	char shown[NORD2_ATQ_CMD], last[96];
	const char *t = sweet_atq_cur.text;
	unsigned b = sweet_atq_cur.len, s0, i, n;
	int ok = 0;

	if (lines >= 400) {
		return;
	}
	lines++;
	for (i = 0; i < sizeof(full) / sizeof(full[0]); i++) {
		if (!strncmp(sweet_atq_cur.cmd, full[i], strlen(full[i]))) {
			ok = 1;
		}
	}
	strlcpy(shown, sweet_atq_cur.cmd, sizeof(shown));
	if (!ok) {
		for (i = 0; shown[i] && shown[i] != '='; i++) {
		}
		if (shown[i] == '=' && i + 5 < sizeof(shown)) {
			shown[i + 1] = 0;
			strlcat(shown, "...", sizeof(shown));
		} else if (shown[0] == 'A' && shown[1] == 'T' && shown[2] == 'D') {
			strlcpy(shown, "ATD<number not logged>", sizeof(shown));   /* 0277af: ATD has no '=' */
		} else if (i > 3) {
			strlcpy(shown + 3, "<not logged>", sizeof(shown) - 3);
		}
	}
	while (b > 0 && (t[b - 1] == '\n' || t[b - 1] == '\r')) {
		b--;
	}
	s0 = b;
	while (s0 > 0 && t[s0 - 1] != '\n') {
		s0--;
	}
	n = b - s0 < sizeof(last) - 1 ? b - s0 : (unsigned)sizeof(last) - 1;
	memcpy(last, t + s0, n);
	last[n] = 0;
	printf("NORD2 AT! [bb] %s -> %s (%u ms)\n", shown, final == NORD2_AT_FINAL_TIMEOUT ? "timeout/modem gone" : (n ? last : "ERROR"),
	    sweet_tick_ms - sweet_atq_cur.t0);
}

/* the bridge query in flight ended (final code, timeout or modem gone); its callback runs here, no AT lock held */
static void
sweet_atq_finish(int final)
{
	nord2_at_done_t done = sweet_atq_cur.done;
	void *ctx = sweet_atq_cur.ctx;

	sweet_atq_cur.done = NULL;
	sweet_atq_cur.pdu_pending = 0;
	if (sweet_atq_cur.has_pdu) {                            /* 0277r */
		sweet_atq_cur.has_pdu = 0;
		sweet_atq_pdu_release();
	}
	if (final == NORD2_AT_FINAL_OK) {
		sweet_atq_ok++;
	} else if (final == NORD2_AT_FINAL_ERROR) {
		sweet_atq_err++;
	} else {
		sweet_atq_tmo++;
	}
	if (final != NORD2_AT_FINAL_OK) {
		sweet_atq_fail_line(final);                 /* 0277z */
	}
	if (done != NULL) {
		done(ctx, final, sweet_atq_cur.text, sweet_atq_cur.len);
	}
}

/* service thread: fail everything if the modem died, else send the next bridge query when the channel is idle */
static void
sweet_atq_tick(void)
{
	char cmd[NORD2_ATQ_CMD];
	nord2_at_done_t done;
	void *ctx;
	int has_pdu;

	if (sweet_md_dead) {
		if (sweet_at_owner == 3) {
			sweet_at_owner = 0;
			sweet_at_pfx[0] = 0;
			sweet_atq_finish(NORD2_AT_FINAL_TIMEOUT);
		}
		for (;;) {
			lck_mtx_lock(&sweet_n2at_mtx);
			if (sweet_atq_count == 0) {
				lck_mtx_unlock(&sweet_n2at_mtx);
				return;
			}
			done = sweet_atq[sweet_atq_head].done;
			ctx = sweet_atq[sweet_atq_head].ctx;
			if (sweet_atq[sweet_atq_head].has_pdu) {
				sweet_atq_pdu_busy = 0;             /* 0277r */
			}
			sweet_atq_head = (sweet_atq_head + 1) % NORD2_ATQ_N;
			sweet_atq_count--;
			lck_mtx_unlock(&sweet_n2at_mtx);
			sweet_atq_tmo++;
			done(ctx, NORD2_AT_FINAL_TIMEOUT, "", 0);
		}
	}
	if (!sweet_md_ready || sweet_at_owner != 0 || sweet_n2at_txh != sweet_n2at_txt || sweet_atq_count == 0) {
		return;
	}
	lck_mtx_lock(&sweet_n2at_mtx);
	if (sweet_atq_count == 0) {
		lck_mtx_unlock(&sweet_n2at_mtx);
		return;
	}
	memcpy(cmd, sweet_atq[sweet_atq_head].cmd, sizeof(cmd));
	done = sweet_atq[sweet_atq_head].done;
	ctx = sweet_atq[sweet_atq_head].ctx;
	has_pdu = sweet_atq[sweet_atq_head].has_pdu;
	sweet_atq_head = (sweet_atq_head + 1) % NORD2_ATQ_N;
	sweet_atq_count--;
	lck_mtx_unlock(&sweet_n2at_mtx);
	sweet_atq_cur.done = done;
	sweet_atq_cur.ctx = ctx;
	sweet_atq_cur.len = 0;
	sweet_atq_cur.text[0] = 0;
	sweet_atq_cur.has_pdu = sweet_atq_cur.pdu_pending = has_pdu;      /* 0277r */
	sweet_atq_cur.tmo_ms = has_pdu || !strncmp(cmd, "ATD", 3) || !strncmp(cmd, "ATA", 3) || !strncmp(cmd, "ATH", 3) ||
	    !strncmp(cmd, "AT+CHLD", 7) || !strncmp(cmd, "AT+CGACT=", 9) || !strncmp(cmd, "AT+EAPNACT=", 11) ||
	    !strncmp(cmd, "AT+CFUN=", 8) ? NORD2_ATQ_LONG_MS : NORD2_ATQ_MS;   /* 0277z: PDN activation waits for the network;
	                                                                     * 0277am: so does a radio off/on (bridge IMS step) */
	memcpy(sweet_atq_cur.cmd, cmd, sizeof(sweet_atq_cur.cmd));
	sweet_atq_cur.t0 = sweet_tick_ms;
	sweet_atq_sent++;
	sweet_at_send_as(cmd, 3);
}

/* p1: send queued /dev/n2at bytes (up to 1 KB per CCCI packet) when no internal command is in flight */
static void
sweet_at_user_tx(void)
{
	static uint8_t pkt[16 + 1024];
	uint32_t n, i, cr = 0;

	if (!sweet_md_ready || sweet_md_dead || sweet_at_owner == 1 || sweet_at_owner == 3 || sweet_n2at_txh == sweet_n2at_txt) {
		return;
	}
	lck_mtx_lock(&sweet_n2at_mtx);
	n = sweet_n2at_txh - sweet_n2at_txt;
	if (n > 1024) {
		n = 1024;
	}
	for (i = 0; i < n; i++) {
		pkt[16 + i] = sweet_n2at_txb[(sweet_n2at_txt + i) % SWEET_N2AT_TXSZ];
		cr += pkt[16 + i] == '\r' || pkt[16 + i] == 0x1a;
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	sweet_wr32(pkt, 0);
	sweet_wr32(pkt + 4, 16 + n);
	sweet_wr32(pkt + 8, 12);                            /* CCCI_UART2_TX */
	sweet_wr32(pkt + 12, 0);
	if (sweet_ring_tx(5, pkt, 16 + n) != 0) {
		return;                                     /* ring full: retried on the next pass */
	}
	lck_mtx_lock(&sweet_n2at_mtx);
	sweet_n2at_txt += n;
	wakeup(&sweet_n2at_txt);
	if (sweet_n2at_wsel_on) {
		sweet_n2at_wsel_on = 0;
		selwakeup(&sweet_n2at_wsel);
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	sweet_at_owner = 2;
	sweet_at_owner_ms = sweet_tick_ms;
	sweet_at_pfx[0] = 0;
	sweet_at_user_cmds += cr;
	sweet_at_user_bytes += n;
}

/* p1: every 60 s in the steady service: AT+CEREG?, AT+COPS?, AT+CSQ one at a time, then one status line */
static void
sweet_st_tick(void)
{
	static const char *const q[3] = { "AT+CEREG?\r", "AT+COPS?\r", "AT+CSQ\r" };
	char nst[192];

	if (!sweet_steady || !sweet_md_ready || sweet_md_dead) {
		return;
	}
	if (sweet_st_step < 0) {
		if ((int32_t)(sweet_tick_ms - sweet_st_next_ms) < 0) {
			return;
		}
		sweet_st_next_ms = sweet_tick_ms + 60000;
		sweet_st_step = 0;
		sweet_st_reg = sweet_st_act = sweet_st_mode = sweet_st_oact = sweet_st_rssi = sweet_st_ber = -1;
		sweet_st_oper[0] = 0;
	}
	if (sweet_at_owner != 0 || sweet_n2at_txh != sweet_n2at_txt) {
		return;                                     /* channel busy: the user goes first */
	}
	if (sweet_st_step < 3) {
		sweet_at_send(q[sweet_st_step]);
		sweet_st_step++;
		return;
	}
	sweet_net_status_fmt(nst, sizeof(nst));            /* N2NET */
	printf("NORD2 MODEM: status up %u s: EPS reg %d act %d | COPS %d \"%s\" act %d | CSQ %d,%d | fs %u ops %u err | ccci rx %u tx %u | "
	    "n2at %s/%u opens, %u cmds %u B, %u drops, AT rx %u B | net %s\n", sweet_tick_ms / 1000, sweet_st_reg, sweet_st_act, sweet_st_mode,
	    sweet_st_oper, sweet_st_oact, sweet_st_rssi, sweet_st_ber, sweet_fs_nops, sweet_fs_nerr, sweet_cnt_rx, sweet_cnt_tx,
	    sweet_n2at_isopen ? "open" : "closed", sweet_n2at_opens, sweet_at_user_cmds, sweet_at_user_bytes, sweet_n2at_drops,
	    sweet_at_rxbytes, nst);
	if (sweet_atq_sent || sweet_atq_full) {                /* 0277 */
		printf("NORD2 MODEM: bb AT queries: sent %u ok %u error %u timeout %u, queue full %u, waiting %u\n", sweet_atq_sent,
		    sweet_atq_ok, sweet_atq_err, sweet_atq_tmo, sweet_atq_full, sweet_atq_count);
	}
	sweet_st_step = -1;
}

/* called from the watch/service loop: the boot script once md_state is READY (timing as c8x), user bytes, the status poll */
static void
sweet_at_tick(void)
{
	if (sweet_at_owner != 0 && sweet_tick_ms - sweet_at_owner_ms >
	    (sweet_at_owner == 1 ? 30000U : (sweet_at_owner == 3 ? sweet_atq_cur.tmo_ms : 120000U))) {
		int was = sweet_at_owner;
		sweet_at_owner = 0;                         /* p1: no final result code: give the channel back */
		sweet_at_pfx[0] = 0;
		/* 0277af: forget the timed-out command's state too: an ATD/ATA without a final result must not keep later NO CARRIER/BUSY
		 * lines bound to it, and a half-received line is dropped (the next command starts clean) */
		if (sweet_at_dialcmd || sweet_at_len) {
			printf("NORD2 AT: owner %d timed out%s%s; channel state reset\n", was, sweet_at_dialcmd ? " (dial in flight)" : "",
			    sweet_at_len ? " (partial line dropped)" : "");
		}
		sweet_at_dialcmd = 0;
		sweet_at_len = 0;
		if (was == 3) {
			sweet_atq_finish(NORD2_AT_FINAL_TIMEOUT);   /* 0277 */
		}
	}
	if (sweet_md_ready && sweet_at_step < SWEET_AT_N) {
		if (!sweet_at_armed) {
			sweet_at_armed = 1;
			sweet_at_next_ms = sweet_tick_ms + 2000;    /* let the modem settle after HS2 */
		} else if ((int32_t)(sweet_tick_ms - sweet_at_next_ms) >= 0 && sweet_at_owner != 2 && sweet_at_owner != 3) {
			sweet_at_send(sweet_at_script[sweet_at_step].cmd);
			sweet_at_next_ms = sweet_tick_ms + sweet_at_script[sweet_at_step].wait_ms;
			sweet_at_step++;
		}
	}
	sweet_at_user_tx();
	sweet_atq_tick();                                   /* 0277: bridge queries after the user, before the status poll */
	sweet_st_tick();
}

/* ---- p1: /dev/n2at character device ---- */
static int
sweet_n2at_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	int err = 0;

	lck_mtx_lock(&sweet_n2at_mtx);
	if (sweet_n2at_isopen) {
		err = EBUSY;
	} else {
		sweet_n2at_isopen = 1;
		sweet_n2at_opens++;
		sweet_n2at_rxt = sweet_n2at_rxh;            /* start with an empty receive ring */
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	return err;
}

static int
sweet_n2at_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	lck_mtx_lock(&sweet_n2at_mtx);
	sweet_n2at_isopen = 0;
	sweet_n2at_rxt = sweet_n2at_rxh;                    /* queued TX bytes are still sent */
	if (sweet_n2at_rsel_on) {
		sweet_n2at_rsel_on = 0;
		selthreadclear(&sweet_n2at_rsel);
	}
	if (sweet_n2at_wsel_on) {
		sweet_n2at_wsel_on = 0;
		selthreadclear(&sweet_n2at_wsel);
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	return 0;
}

static int
sweet_n2at_read(__unused dev_t dev, struct uio *uio, int ioflag)
{
	uint8_t buf[512];
	int err = 0, got = 0;

	while (uio_resid(uio) > 0) {
		uint32_t n, i;
		lck_mtx_lock(&sweet_n2at_mtx);
		while (sweet_n2at_rxh == sweet_n2at_rxt) {
			if (got || (ioflag & IO_NDELAY)) {
				lck_mtx_unlock(&sweet_n2at_mtx);
				return got ? 0 : EWOULDBLOCK;
			}
			err = msleep(&sweet_n2at_rxh, &sweet_n2at_mtx, PCATCH | PZERO, "n2at", NULL);
			if (err) {
				lck_mtx_unlock(&sweet_n2at_mtx);
				return err;
			}
		}
		n = sweet_n2at_rxh - sweet_n2at_rxt;
		if (n > sizeof(buf)) {
			n = sizeof(buf);
		}
		if ((user_ssize_t)n > uio_resid(uio)) {
			n = (uint32_t)uio_resid(uio);
		}
		for (i = 0; i < n; i++) {
			buf[i] = sweet_n2at_rxb[(sweet_n2at_rxt + i) % SWEET_N2AT_RXSZ];
		}
		sweet_n2at_rxt += n;
		lck_mtx_unlock(&sweet_n2at_mtx);
		err = uiomove((caddr_t)buf, (int)n, uio);
		if (err) {
			return err;
		}
		got = 1;
	}
	return 0;
}

static int
sweet_n2at_write(__unused dev_t dev, struct uio *uio, int ioflag)
{
	uint8_t buf[256];
	int err = 0, put = 0;

	if (sweet_md_dead) {
		return ENXIO;
	}
	while (uio_resid(uio) > 0) {
		uint32_t n = sizeof(buf), room, i;
		lck_mtx_lock(&sweet_n2at_mtx);
		while ((room = SWEET_N2AT_TXSZ - (sweet_n2at_txh - sweet_n2at_txt)) == 0) {
			if (put || (ioflag & IO_NDELAY)) {
				lck_mtx_unlock(&sweet_n2at_mtx);
				return put ? 0 : EWOULDBLOCK;
			}
			err = msleep(&sweet_n2at_txt, &sweet_n2at_mtx, PCATCH | PZERO, "n2atw", NULL);
			if (err) {
				lck_mtx_unlock(&sweet_n2at_mtx);
				return err;
			}
		}
		lck_mtx_unlock(&sweet_n2at_mtx);
		if (n > room) {
			n = room;
		}
		if ((user_ssize_t)n > uio_resid(uio)) {
			n = (uint32_t)uio_resid(uio);
		}
		err = uiomove((caddr_t)buf, (int)n, uio);
		if (err) {
			return err;
		}
		lck_mtx_lock(&sweet_n2at_mtx);                  /* only the service thread consumes: room can only have grown */
		for (i = 0; i < n; i++) {
			sweet_n2at_txb[sweet_n2at_txh++ % SWEET_N2AT_TXSZ] = buf[i];
		}
		sweet_svc_kick = 1;
		wakeup(&sweet_svc_kick);
		lck_mtx_unlock(&sweet_n2at_mtx);
		put = 1;
	}
	return 0;
}

static int
sweet_n2at_ioctl(__unused dev_t dev, u_long cmd, caddr_t data, __unused int fflag, __unused struct proc *p)
{
	switch (cmd) {
	case FIONBIO:
	case FIOASYNC:
		return 0;                                   /* O_NONBLOCK is passed per call (IO_NDELAY) */
	case FIONREAD:
		lck_mtx_lock(&sweet_n2at_mtx);
		*(int *)data = (int)(sweet_n2at_rxh - sweet_n2at_rxt);
		lck_mtx_unlock(&sweet_n2at_mtx);
		return 0;
	default:
		return ENOTTY;
	}
}

static int
sweet_n2at_select(__unused dev_t dev, int which, void *wql, struct proc *p)
{
	int ready = 0;

	lck_mtx_lock(&sweet_n2at_mtx);
	if (which == FREAD) {
		if (sweet_n2at_rxh != sweet_n2at_rxt) {
			ready = 1;
		} else {
			selrecord(p, &sweet_n2at_rsel, wql);
			sweet_n2at_rsel_on = 1;
		}
	} else if (which == FWRITE) {
		if (sweet_n2at_txh - sweet_n2at_txt < SWEET_N2AT_TXSZ) {
			ready = 1;
		} else {
			selrecord(p, &sweet_n2at_wsel, wql);
			sweet_n2at_wsel_on = 1;
		}
	}
	lck_mtx_unlock(&sweet_n2at_mtx);
	return ready;
}

static const struct cdevsw sweet_n2at_cdevsw = {
	.d_open       = sweet_n2at_open,
	.d_close      = sweet_n2at_close,
	.d_read       = sweet_n2at_read,
	.d_write      = sweet_n2at_write,
	.d_ioctl      = sweet_n2at_ioctl,
	.d_stop       = eno_stop,
	.d_reset      = eno_reset,
	.d_ttys       = NULL,
	.d_select     = sweet_n2at_select,
	.d_mmap       = eno_mmap,
	.d_strategy   = eno_strat,
	.d_reserved_1 = eno_getc,
	.d_reserved_2 = eno_putc,
	.d_type       = 0,
};

static void
sweet_n2at_init(void)
{
	int maj;

	if (sweet_n2at_major >= 0) {
		return;
	}
	maj = cdevsw_add(-1, &sweet_n2at_cdevsw);
	if (maj < 0) {
		printf("NORD2 MODEM: /dev/n2at: cdevsw_add failed (%d)\n", maj);
		return;
	}
	sweet_n2at_major = maj;
	if (devfs_make_node(makedev(maj, 0), DEVFS_CHAR, UID_ROOT, GID_WHEEL, 0666, "n2at") == NULL) {
		printf("NORD2 MODEM: /dev/n2at: devfs_make_node failed (major %d)\n", maj);
		return;
	}
	printf("NORD2 MODEM: /dev/n2at ready (major %d)\n", maj);
}


/*
 * N2NET: data path for the PDN of cid 1 (ccmni0): MediaTek DPMAIF (UL DRB queues, DL BAT + frag BAT + PIT) driven by polling from the
 * modem thread, bridged to an XNU point-to-point IP interface "pdp_ip0" (IFNET_FAMILY_CELLULAR, legacy output model).
 *
 * Hardware start = Linux dpmaif_start() (hif/ccci_hif_dpmaif.c + hif/dpmaif_drv.c, MT6297 paths) as one register sequence:
 * clocks + infracfg_ao_mem bit 10, common init (CG_EN, WDMA, MD bus remap, L1 mask, power flag), L2 interrupt init, DL BAT/PIT/frag
 * init, BAT + frag add and enable, the four UL queues.  Linux runs it before md_cd_power_on(): at the first boot with the LK-left MD1
 * still on, 20 ms before md_start_platform powers MD1 off (vendor log 71.470 remap -> 71.490 POWER OFF), and after a modem reset with
 * MD1 off (149.1 power off, 167.8 remap, 167.88 power on).  Here it runs in the second state: inside the tight MD1 cycle, right after MD1
 * off and before MD1 on + BOOT_UP, so no powered modem ever runs against a half-programmed DPMAIF/remap.  q10ab wrote the same remap with
 * the LK-left MD1 on and then idled >3 s (3 s pause per step) before the cycle: the AP hung in that pause.  c8x's partial init after
 * MD1 on stays as it was (it repeats three of these writes with the same values).
 * Memory: one physically contiguous kmem_alloc_contig region (cacheable, KMA_DATA like the UFS driver), maintained with
 * clean_dcache64/flush_dcache64 by PA (SWEET has no __ARM_COHERENT_IO__); addresses may be above 4 GiB (DPMAIF is 36-bit; Linux on this
 * phone placed its DRBs at 0x1_23b34000).
 * Boot-arg nord2_net (default 1): bit0 data path on, bit1 skip the MD remap, bit2 no kernel self-test, bit3 no unscoped default route,
 * bit4 leave the DPMAIF L2 interrupts unmasked as Linux does (default: all masked, the GIC line is not used).
 * p2 (persistent service): the steady loop keeps polling DL/UL forever (every 2 ms after traffic, at most 5 ms apart while pdp_ip0
 * is up; a packet from the IP stack wakes it at once; no DPMAIF IRQ: SPI 209 would need an IOKit interrupt nub), pdp_ip0 stays up and follows a new PDN address, a global default route is
 * added only while no other default exists (re-checked every 30 s), the PDN address/DNS/MTU are published in sysctl net.nord2.pdp
 * (the kernel cannot configure the iOS resolver: configd owns it), and the 60 s status line carries the net counters.
 */
#include <sys/kpi_mbuf.h>
#include <libkern/OSAtomic.h>
#include <sys/kpi_socket.h>
#include <sys/sockio.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/kpi_interface.h>
#include <net/kpi_protocol.h>
#include <net/route.h>
#include <net/route_private.h>
#include <netinet/in.h>
#include <sys/sysctl.h>
#include <kern/locks.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

extern ppnum_t pmap_find_phys(pmap_t pmap, addr64_t va);
extern void flush_dcache64(addr64_t addr, unsigned count, int phys);   /* osfmk/arm/caches_internal.h (MACH_KERNEL_PRIVATE) */
extern void clean_dcache64(addr64_t addr, unsigned count, int phys);

static TUNABLE(uint32_t, nord2_net, "nord2_net", 1);

static uint8_t *n2d_va;                     /* the DMA region */
static uint64_t n2d_pa;

static inline uint32_t
n2d_rd(unsigned b, uint32_t off)
{
	return sweet_n2d_blk[b][off / 4];
}

static inline void
n2d_wr(unsigned b, uint32_t off, uint32_t v)
{
	sweet_n2d_blk[b][off / 4] = v;
	__asm__ volatile ("dsb sy" ::: "memory");   /* dpmaif_write32: writel + mb() */
}

static void
n2d_dma_clean(uint32_t off, uint32_t len)
{
	clean_dcache64((addr64_t)(n2d_pa + off), len, TRUE);
	__asm__ volatile ("dsb sy" ::: "memory");
}

static void
n2d_dma_inval(uint32_t off, uint32_t len)
{
	flush_dcache64((addr64_t)(n2d_pa + off), len, TRUE);    /* clean+invalidate: the CPU never dirties HW-written lines */
}

static uint32_t
n2net_now_ms(void)
{
	uint64_t ns;

	absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
	return (uint32_t)(ns / 1000000ULL);
}

/* SWEET_NET_CORE_BEGIN */
/*
 * DPMAIF hardware rings, descriptor formats and the in-kernel self-test.  Host-testable: it only uses n2d_rd/n2d_wr (registers),
 * n2d_va/n2d_pa (the one DMA region), n2d_dma_clean/n2d_dma_inval (cache maintenance), n2net_now_ms and n2net_deliver from the glue.
 * Sources: nord2-current-kernel eccci hif/dpmaif_drv.c, hif/ccci_hif_dpmaif.c, hif/dpmaif_reg.h, mt6885/ccci_config.h (MT6297,
 * HW_FRG_FEATURE_ENABLE, HW_CHECK_SUM_ENABLE, _HW_REORDER_SW_WORKAROUND_, _97_REORDER_BAT_PAGE_TABLE_, BAT_CNT_BURST_UPDATE).
 */
#define N2D_AO_UL    0u          /* 0x10014000 */
#define N2D_AO_DL    1u          /* 0x10014400 */
#define N2D_AO_MD    2u          /* 0x10014800  MISC_AO (MD bus remap) */
#define N2D_PD_UL    3u          /* 0x1022d000 */
#define N2D_PD_DL    4u          /* 0x1022d100 */
#define N2D_PD_WDMA  5u          /* 0x1022d300 */
#define N2D_PD_MISC  6u          /* 0x1022d400 */
#define N2D_MD_MISC  7u          /* 0x1022c000 */

#define N2D_PIT_N    16384u      /* DPMAIF_DL_PIT_ENTRY_SIZE, as Linux (the dummy-PIT index 1024 is relative to it) */
#define N2D_BAT_N    128u        /* Linux 8192; > 2 x DPMAIF_HW_CHK_BAT_NUM (62) */
#define N2D_FRG_N    128u
#define N2D_DRB_N    256u        /* per UL queue, Linux 2048 */
#define N2D_TXQ_N    4u          /* DPMAIF_TXQ_NUM: all four are configured like Linux, only q0 (ccmni0 DATA_TX_Q) is used */
#define N2D_BUF      2048u       /* stride of every packet buffer */
#define N2D_PKT_SZ   1664u       /* DPMAIF_PKT_SIZE = 128 * 13 */
#define N2D_FRG_SZ   1920u       /* DPMAIF_FRG_SIZE = 128 * 15 */
#define N2D_HW_MTU   3080u       /* DPMAIF_HW_MTU_SIZE = 3 * 1024 + 8 */
#define N2D_APIT0    1024u       /* DPMAIF_DUMMY_PIT_AIDX */
#define N2D_DUMMY_MAX 0x3FFFFFu  /* DPMAIF_DUMMY_PIT_MAX_NUM */
#define N2D_TRY      200000u     /* bound of every register poll (Linux: 1600000) */
#define N2D_TRY_IDLE 20000u      /* polls of the dummy-PIT reload path (DL idle) */

#define N2D_OFF_PIT  0x00000u    /* N2D_PIT_N x 16 B = 256 KiB */
#define N2D_OFF_BAT  0x40000u    /* N2D_BAT_N x 8 B */
#define N2D_OFF_FRG  0x40400u    /* N2D_FRG_N x 8 B */
#define N2D_OFF_DRB  0x41000u    /* q0..q3: N2D_DRB_N x 8 B each */
#define N2D_OFF_TX   0x44000u    /* q0 packet slots, one per (msg, payload) DRB pair: 128 x 2 KiB */
#define N2D_OFF_RX   0x84000u    /* normal BAT buffers, buffer i <-> BAT entry i */
#define N2D_OFF_RXF  0xC4000u    /* frag BAT buffers */
#define N2D_DMA_SIZE 0x104000u   /* 1040 KiB = 65 x 16 KiB pages */

#define N2D_UL_EN    0x01FFFC1Fu /* AP_UL_L2INTR_En_Msk (MT6297: QDONE | MD_NOTRDY | PWR_NOTRDY | LEN_ERR, 5 queues) */
#define N2D_DL_EN    0x00000043u /* AP_DL_L2INTR_En_Msk: QDONE | SKB_LEN_ERR | MTU_ERR */

/* MISC_AO remap of the MD bus banks: Linux drv_dpmaif_md_hw_bus_remap with md_base_at_ap 0xe0000000 ([remap]-A..-1D in the vendor log) */
_Static_assert(N2D_OFF_BAT >= N2D_PIT_N * 16u && N2D_OFF_FRG >= N2D_OFF_BAT + N2D_BAT_N * 8u &&
    N2D_OFF_DRB >= N2D_OFF_FRG + N2D_FRG_N * 8u && N2D_OFF_TX >= N2D_OFF_DRB + N2D_TXQ_N * N2D_DRB_N * 8u &&
    N2D_OFF_RX >= N2D_OFF_TX + N2D_DRB_N / 2u * N2D_BUF && N2D_OFF_RXF >= N2D_OFF_RX + N2D_BAT_N * N2D_BUF &&
    N2D_DMA_SIZE >= N2D_OFF_RXF + N2D_FRG_N * N2D_BUF && (N2D_DMA_SIZE & 0x3FFFu) == 0, "N2D DMA layout");

static const uint32_t n2d_remap[8] = {
	0x00e300e1, 0x00e700e5, 0x00eb00e9, 0x00ef00ed, 0x00f300f1, 0x00f700f5, 0x00fb00f9, 0x00ff00fd
};

static struct {
	int started;
	int err[4];                         /* hw start: intr, rx, rx enable, quiet */
	uint32_t pit_rd, pit_wr;
	uint32_t bat_wr, bat_rel, frg_wr, frg_rel;
	uint8_t bat_busy[N2D_BAT_N], frg_busy[N2D_FRG_N];  /* bid_btable: 1 = given to the HW */
	uint32_t pend_pit, pend_bat, pend_frg;
	uint32_t dummy_cnt, dummy_idx;
	int reload;
	int skb;                            /* normal buffer of the packet being assembled, -1 none */
	unsigned cur_ch, cur_dp, cur_csum;
	uint32_t asm_len;
	int asm_bad;
	uint8_t asm_buf[4096];
	uint32_t drb_wr, drb_rel;
	uint32_t tx_pkts, tx_bytes, tx_drop, tx_busy, tx_err;
	uint32_t rx_pkts, rx_bytes, rx_drop, rx_msg, rx_dummy, rx_err, rx_frag, reloads, hw_err;
	unsigned log_pit, log_err, log_msg;
} n2d;

static uint32_t
n2d_le32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void
n2d_st32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static void
n2d_rmw(unsigned b, uint32_t off, uint32_t clr, uint32_t set)
{
	n2d_wr(b, off, (n2d_rd(b, off) & ~clr) | set);
}

/* (reg & mask) == want, bounded */
static int
n2d_wait_n(unsigned b, uint32_t off, uint32_t mask, uint32_t want, unsigned max)
{
	unsigned i;

	for (i = 0; i < max; i++) {
		if ((n2d_rd(b, off) & mask) == want) {
			return 0;
		}
	}
	return -1;
}

static int
n2d_wait(unsigned b, uint32_t off, uint32_t mask, uint32_t want)
{
	return n2d_wait_n(b, off, mask, want, N2D_TRY);
}

/* Linux "check mask sts": loop while every bit of mask still reads 1 */
static int
n2d_wait_not_all(unsigned b, uint32_t off, uint32_t mask)
{
	unsigned i;

	for (i = 0; i < N2D_TRY; i++) {
		if ((n2d_rd(b, off) & mask) != mask) {
			return 0;
		}
	}
	return -1;
}

/* BAT/PIT INIT and ADD registers: wait NOT_READY (bit 31) clear, write, optionally wait for the HW to take it */
static int
n2d_kick_n(unsigned b, uint32_t off, uint32_t v, int wait_after, unsigned max)
{
	if (n2d_wait_n(b, off, 1u << 31, 0, max)) {
		return -1;
	}
	n2d_wr(b, off, v);
	if (wait_after && n2d_wait_n(b, off, 1u << 31, 0, max)) {
		return -2;
	}
	return 0;
}

static int
n2d_kick(unsigned b, uint32_t off, uint32_t v, int wait_after)
{
	return n2d_kick_n(b, off, v, wait_after, N2D_TRY);
}

/* drv_dpmaif_set_dl_interrupt_mask: AO copy of the DL L2 mask in RDY_CHK_THRES[6:0] */
static void
n2d_dl_isr_mask(uint32_t mask)
{
	n2d_rmw(N2D_AO_DL, 0x0C, 0x7Fu, mask & 0x7Fu);
}

/* drv_dpmaif_common_hw_init (the clocks and infracfg_ao_mem bit 10 are done by the glue) */
static void
n2d_hw_common(int remap)
{
	unsigned i;

	n2d_wr(N2D_PD_MISC, 0x68, 0x7Fu);                           /* AP_MISC_CG_EN: HW CG off */
	n2d_rmw(N2D_PD_WDMA, 0x14, 0xFu << 28, 0xEu << 28);        /* WDMA_WR_CHNL_CMD_CON3: OSTD 0xE */
	n2d_rmw(N2D_PD_WDMA, 0x00, 0x0000FFFFu, 0);                 /* WDMA_WR_CMD_CON0: CTRL_INTVAL/INTVAL_MIN 0 */
	if (remap) {
		n2d_wr(N2D_AO_MD, 0x20, (1u << 0) | (1u << 8));     /* REMAP_DOMAIN: AW/AR domain 1 (MD) */
		n2d_wr(N2D_AO_MD, 0x24, (6u << 0) | (7u << 8));     /* REMAP_CACHE: bank0 6, bank1 7 */
		for (i = 0; i < 8; i++) {
			n2d_wr(N2D_AO_MD, 0x30 + 4u * i, n2d_remap[i]); /* BANK0_MAPA..D, BANK1_MAPA..D */
		}
		n2d_rmw(N2D_AO_MD, 0x04, 0, 1u);                    /* MISC_AO_CFG1: remap enable */
	}
	n2d_wr(N2D_AO_UL, 0x8C, (1u << 9) | (1u << 10) | (1u << 15) | (1u << 16));   /* AP_L1TIMR0 */
	n2d_wr(N2D_PD_DL, 0xA8, 0xFFu);                             /* DL_RESERVE_RW: power on/off flag */
}

/* drv_dpmaif_intr_hw_init (MT6297) */
static int
n2d_hw_intr(void)
{
	int rc = 0;

	n2d_wr(N2D_PD_MISC, 0x00, 0xFFFFFFFFu);                     /* AP_L2TISAR0: clear UL status */
	n2d_wr(N2D_AO_UL, 0x84, N2D_UL_EN);                         /* AP_L2TIMCR0 */
	n2d_wr(N2D_AO_UL, 0x88, ~N2D_UL_EN);                        /* AP_L2TIMSR0 */
	if (n2d_wait_not_all(N2D_AO_UL, 0x80, N2D_UL_EN)) {
		rc |= 1;
	}
	n2d_wr(N2D_PD_MISC, 0x50, 0xFFFFFFFFu);                     /* APDL_L2TISAR0: clear DL status */
	n2d_wr(N2D_AO_UL, 0x94, N2D_DL_EN);                         /* APDL_L2TIMCR0 */
	n2d_wr(N2D_AO_UL, 0x98, ~N2D_DL_EN);                        /* APDL_L2TIMSR0 */
	n2d_dl_isr_mask(~N2D_DL_EN);
	if (n2d_wait_not_all(N2D_AO_UL, 0x90, N2D_DL_EN)) {
		rc |= 2;
	}
	n2d_wr(N2D_PD_MISC, 0x60, 0xFFFFFFFFu);                     /* AP_IP_BUSY: clear */
	n2d_wr(N2D_AO_UL, 0x9C, 0);                                 /* AP_DL_UL_IP_BUSY_MASK: unmask */
	return -rc;
}

/* BAT (normal or frag) / PIT base + size: CON0 = pa[31:0], CON1[31:24] = pa[39:32], CON1[15:0] = entries */
static void
n2d_set_table(uint32_t con0, uint32_t con1, uint64_t pa, uint32_t n)
{
	n2d_wr(N2D_PD_DL, con0, (uint32_t)pa);
	n2d_rmw(N2D_PD_DL, con1, 0xFF000000u, ((uint32_t)(pa >> 32) & 0xFFu) << 24);
	n2d_rmw(N2D_PD_DL, con1, 0x0000FFFFu, n & 0xFFFFu);
}

/* dpmaif_rx_hw_init (MT6297, frag + checksum features, reorder workaround) */
static int
n2d_hw_rx(void)
{
	int rc = 0;

	n2d_rmw(N2D_AO_DL, 0x00, 0x0000FF00u, (64u / 16u) << 8);              /* remain minsz: DPMAIF_HW_BAT_REMAIN 64 */
	n2d_rmw(N2D_AO_DL, 0x08, 0x0001FF00u, (N2D_PKT_SZ / 128u) << 8);      /* BAT buffer size */
	n2d_rmw(N2D_AO_DL, 0x08, 0x000000FFu, 0);                              /* BAT reserve length 0 */
	n2d_rmw(N2D_AO_DL, 0x00, 0xFFFF0000u, 1u << 16);                       /* bid max count 1 */
	n2d_rmw(N2D_AO_DL, 0x0C, 3u << 22, 1u << 23);                          /* packet align 64 */
	n2d_wr(N2D_AO_DL, 0x04, N2D_HW_MTU);                                    /* PKTINFO_CON1: MTU */
	n2d_rmw(N2D_AO_DL, 0x08, 0xFF000000u, 124u << 24);                     /* PIT check num */
	n2d_rmw(N2D_AO_DL, 0x0C, 0x3Fu << 16, 62u << 16);                      /* BAT check threshold */
	n2d_rmw(N2D_AO_DL, 0x00, 0x000000FFu, 64u);                            /* check RB PIT num */
	n2d_rmw(N2D_PD_DL, 0x0C, 0, 1u << 22);                                 /* performance: BAT cache */
	n2d_rmw(N2D_AO_DL, 0x0C, 0, 1u << 13);                                 /* performance: PIT burst */
	n2d_rmw(N2D_PD_DL, 0x34, 0xFFFFu << 16, N2D_APIT0 << 16);              /* reorder workaround: apit index */
	n2d_wr(N2D_PD_UL, 0xA8, N2D_APIT0 | (1u << 31));                       /* UL_RESERVE_RW: tell the MD */
	n2d.dummy_idx = N2D_APIT0;
	n2d.dummy_cnt = 0;
	n2d_set_table(0x08, 0x0C, n2d_pa + N2D_OFF_BAT, N2D_BAT_N);
	n2d_set_table(0x28, 0x2C, n2d_pa + N2D_OFF_PIT, N2D_PIT_N);
	n2d_rmw(N2D_PD_DL, 0x34, 0, 1u);                                       /* PIT enable */
	n2d_rmw(N2D_PD_DL, 0x0C, 1u << 16, 0);                                 /* BAT disabled until the first add */
	if (n2d_kick(N2D_PD_DL, 0x00, 1u | (1u << 31), 1)) {                   /* BAT init done: all settings */
		rc |= 1;
	}
	if (n2d_kick(N2D_PD_DL, 0x20, 1u | (1u << 31), 1)) {                   /* PIT init done */
		rc |= 2;
	}
	n2d_rmw(N2D_AO_DL, 0x30, 1u << 28, 1u << 28);                          /* frag BAT feature on */
	n2d_rmw(N2D_AO_DL, 0x30, 0xFFu << 8, (N2D_FRG_SZ / 128u) << 8);        /* frag buffer size */
	n2d_rmw(N2D_AO_DL, 0x30, 0xFFu, 62u);                                  /* frag check threshold */
	n2d_set_table(0x08, 0x0C, n2d_pa + N2D_OFF_FRG, N2D_FRG_N);
	n2d_rmw(N2D_PD_DL, 0x0C, 1u << 16, 0);
	if (n2d_kick(N2D_PD_DL, 0x00, (1u << 16) | 1u | (1u << 31), 1)) {     /* frag BAT init done */
		rc |= 4;
	}
	n2d_rmw(N2D_AO_DL, 0x0C, 0, 1u << 31);                                 /* DL checksum check on */
	return -rc;
}

/* dpmaif_start after dpmaif_alloc_rx_buf/_frag: give N-1 entries of each table to the HW and enable both (BAT_CNT_BURST_UPDATE) */
static int
n2d_hw_rx_enable(void)
{
	int rc = 0;

	if (n2d_kick(N2D_PD_DL, 0x04, (N2D_FRG_N - 1u) | (1u << 16) | (1u << 31), 0)) {   /* frag BAT add */
		rc |= 1;
	}
	if (n2d_kick(N2D_PD_DL, 0x04, (N2D_BAT_N - 1u) | (1u << 31), 0)) {               /* BAT add */
		rc |= 2;
	}
	n2d_rmw(N2D_PD_DL, 0x0C, 0, 1u << 16);
	if (n2d_kick(N2D_PD_DL, 0x00, (1u << 16) | (1u << 31), 1)) {                     /* all frag queue enable */
		rc |= 4;
	}
	n2d_rmw(N2D_PD_DL, 0x0C, 0, 1u << 16);
	if (n2d_kick(N2D_PD_DL, 0x00, 1u << 31, 1)) {                                    /* all queue enable */
		rc |= 8;
	}
	return -rc;
}

/* dpmaif_tx_hw_init for the four UL queues: DRB size (words), base, ready, arbitration */
static void
n2d_hw_tx(void)
{
	unsigned q;

	for (q = 0; q < N2D_TXQ_N; q++) {
		uint64_t pa = n2d_pa + N2D_OFF_DRB + q * N2D_DRB_N * 8u;
		n2d_rmw(N2D_AO_UL, 0x24 + 0x10u * q, 0x0000FFFFu, (N2D_DRB_N * 8u / 4u) & 0xFFFFu);
		n2d_wr(N2D_AO_UL, 0x20 + 0x10u * q, (uint32_t)pa);
		n2d_rmw(N2D_AO_UL, 0x24 + 0x10u * q, 0xFF000000u, ((uint32_t)(pa >> 32) & 0xFFu) << 24);
		n2d_rmw(N2D_AO_UL, 0x1C, 0, 1u << q);                  /* CHNL_ARB0: ready */
		n2d_rmw(N2D_AO_UL, 0x1C, 0, 1u << (q + 8));            /* CHNL_ARB0: arbitration */
	}
}

/* Polled operation: keep every DPMAIF L2 interrupt masked (the state Linux holds while its rx tasklet/tx-done thread run) */
static void
n2d_hw_quiet(void)
{
	n2d_wr(N2D_AO_UL, 0x88, 0xFFFFFFFFu);
	n2d_wr(N2D_AO_UL, 0x98, 0xFFFFFFFFu);
	n2d_dl_isr_mask(0x7Fu);
}

/* arm BAT (or frag BAT) entry i with its fixed buffer */
static void
n2d_bat_arm(int frag, uint32_t i)
{
	uint32_t tab = (frag ? N2D_OFF_FRG : N2D_OFF_BAT) + i * 8u;
	uint64_t pa = n2d_pa + (frag ? N2D_OFF_RXF : N2D_OFF_RX) + (uint64_t)i * N2D_BUF;

	n2d_st32(n2d_va + tab, (uint32_t)pa);
	n2d_st32(n2d_va + tab + 4, (uint32_t)(pa >> 32) & 0xFFu);  /* buffer_addr_ext */
	n2d_dma_clean(tab, 8);
	if (frag) {
		n2d.frg_busy[i] = 1;
	} else {
		n2d.bat_busy[i] = 1;
	}
}

/* software state + tables as dpmaif_rx_buf_init/alloc_rx_buf/alloc_rx_frag leave them (N-1 entries armed) */
static void
n2d_sw_reset(void)
{
	uint32_t i;

	memset(&n2d, 0, sizeof(n2d));
	n2d.skb = -1;
	memset(n2d_va + N2D_OFF_PIT, 0, N2D_PIT_N * 16u);
	memset(n2d_va + N2D_OFF_DRB, 0, N2D_TXQ_N * N2D_DRB_N * 8u);
	memset(n2d_va + N2D_OFF_BAT, 0, N2D_BAT_N * 8u);
	memset(n2d_va + N2D_OFF_FRG, 0, N2D_FRG_N * 8u);
	n2d_dma_clean(0, N2D_OFF_TX);
	for (i = 0; i + 1u < N2D_BAT_N; i++) {
		n2d_bat_arm(0, i);
	}
	for (i = 0; i + 1u < N2D_FRG_N; i++) {
		n2d_bat_arm(1, i);
	}
	n2d.bat_wr = N2D_BAT_N - 1u;
	n2d.frg_wr = N2D_FRG_N - 1u;
}

/* the whole Linux dpmaif_start register sequence; no printf (runs in the MD1-off window); n2d.err[] keeps the results */
static int
n2d_hw_start(int remap, int quiet)
{
	n2d.started = 0;
	n2d_hw_common(remap);
	n2d.err[0] = n2d_hw_intr();
	n2d.err[1] = n2d_hw_rx();
	n2d.err[2] = n2d.err[1] ? -16 : n2d_hw_rx_enable();
	n2d_hw_tx();
	if (quiet) {
		n2d_hw_quiet();
	}
	n2d.started = n2d.err[1] == 0 && n2d.err[2] == 0;
	return n2d.started ? 0 : -1;
}

/* ---- DL ---- */

/* PIT INIT "only enable bit" update (drv_dpmaif_dl_pit_en + drv_dpmaif_dl_pit_only_update_enable_bit_done) */
static int
n2d_pit_en_only(int en)
{
	n2d_rmw(N2D_PD_DL, 0x34, 1u, en ? 1u : 0u);
	return n2d_kick_n(N2D_PD_DL, 0x20, 1u << 31, 1, N2D_TRY_IDLE);
}

/* drv_dpmaif_check_dl_fifo_idle */
static void
n2d_dl_fifo_idle(void)
{
	unsigned i;

	for (i = 0; i < N2D_TRY_IDLE; i++) {
		uint32_t r = n2d_rd(N2D_PD_DL, 0xCC);                      /* DL_DBG_STA7 */
		if (((r >> 20) & 0x3Fu) == ((r >> 5) & 0x3Fu) && (r & 0x10001u) == 0x10001u) {
			break;
		}
	}
	(void)n2d_wait_n(N2D_PD_DL, 0x258, 1u, 1u, N2D_TRY_IDLE);        /* DMA write done */
}

/* drv_dpmaif_set_dl_idle(true) */
static int
n2d_dl_idle(void)
{
	unsigned n;

	for (n = 0; n < 64u; n++) {
		(void)n2d_pit_en_only(0);
		(void)n2d_wait_n(N2D_PD_DL, 0xB4, 1u << 25, 1u << 25, N2D_TRY_IDLE);   /* DL_DBG_STA1: idle */
		if ((n2d_rd(N2D_AO_DL, 0x2C) & 1u) == 0) {                  /* AO PIT_STA3: enable dropped */
			(void)n2d_wait_n(N2D_PD_DL, 0xB4, 1u << 25, 1u << 25, N2D_TRY_IDLE);
			n2d_dl_fifo_idle();
			return 0;
		}
	}
	return -1;
}

/* drv_dpmaif_dl_add_apit_num: move the dummy-PIT index forward by cnt (reorder workaround reload) */
static int
n2d_add_apit(uint32_t cnt)
{
	uint32_t sta2, ridx, widx, aidx, size, chk, naidx, md;
	int ret;

	n2d_wr(N2D_MD_MISC, 0x100, 0);                                  /* DL_RB_PIT_INIT: force enable off */
	if (n2d_dl_idle()) {
		(void)n2d_pit_en_only(1);                               /* Linux leaves the PIT disabled here; keep DL running */
		n2d_wr(N2D_MD_MISC, 0x100, 1u << 7);
		return -1;
	}
	sta2 = n2d_rd(N2D_AO_DL, 0x28);
	ridx = (sta2 >> 16) & 0xFFFFu;
	widx = sta2 & 0xFFFFu;
	aidx = (n2d_rd(N2D_AO_DL, 0x2C) >> 16) & 0xFFFFu;
	size = n2d_rd(N2D_AO_DL, 0x24) & 0xFFFFu;
	chk = ridx <= aidx ? size - aidx + ridx : ridx - aidx;
	if (cnt + 124u < chk) {
		naidx = aidx + cnt;
		if (naidx >= size) {
			naidx -= size;
		}
		n2d_wr(N2D_PD_DL, 0x28, n2d_rd(N2D_AO_DL, 0x20));
		n2d_wr(N2D_PD_DL, 0x2C, n2d_rd(N2D_AO_DL, 0x24));
		n2d_wr(N2D_PD_DL, 0x30, n2d_rd(N2D_AO_DL, 0x28));
		n2d_wr(N2D_PD_DL, 0x34, ((naidx & 0xFFFFu) << 16) | 1u);
		if (n2d_kick_n(N2D_PD_DL, 0x20, 1u | (1u << 31) | ((widx & 0xFFFFu) << 4), 1, N2D_TRY_IDLE)) {
			(void)n2d_pit_en_only(1);
			n2d_wr(N2D_MD_MISC, 0x100, 1u << 7);
			return -2;
		}
		md = n2d_rd(N2D_PD_UL, 0xA8) & ~(1u << 31);
		md += cnt;
		if (md >= N2D_DUMMY_MAX) {
			md -= N2D_DUMMY_MAX;
		}
		n2d_wr(N2D_PD_UL, 0xA8, md | (1u << 31));
		n2d_wr(N2D_PD_UL, 0xAC, 0xFFu);
		n2d_wr(N2D_MD_MISC, 0x1C, 1u);                              /* MD_L1TIMSR0: notify the MD */
		ret = (int)cnt;
	} else {
		(void)n2d_pit_en_only(1);
		ret = 0;
	}
	n2d_wr(N2D_MD_MISC, 0x100, 1u << 7);
	return ret;
}

/* release consecutive consumed entries from the release cursor and re-arm as many (dpmaif_check_rel_cnt / dpmaif_get_rx_frag) */
static uint32_t
n2d_recycle(int frag)
{
	uint8_t *busy = frag ? n2d.frg_busy : n2d.bat_busy;
	uint32_t n = frag ? N2D_FRG_N : N2D_BAT_N;
	uint32_t *rel = frag ? &n2d.frg_rel : &n2d.bat_rel, *wr = frag ? &n2d.frg_wr : &n2d.bat_wr;
	uint32_t armed = 0;

	while (*rel != *wr && !busy[*rel]) {
		*rel = (*rel + 1u) % n;
	}
	while ((*rel + n - *wr - 1u) % n != 0) {
		n2d_bat_arm(frag, *wr);
		*wr = (*wr + 1u) % n;
		armed++;
	}
	return armed;
}

/* dpmaifq_rx_notify_hw: frag BAT count, BAT count, PIT count; then the dummy-PIT reload */
static int
n2d_rx_notify(void)
{
	if (n2d.pend_pit) {
		if (n2d.pend_frg) {
			if (n2d_kick(N2D_PD_DL, 0x04, (n2d.pend_frg & 0xFFFFu) | (1u << 16) | (1u << 31), 0)) {
				return -1;
			}
			n2d.pend_frg = 0;
		}
		if (n2d.pend_bat) {
			if (n2d_kick(N2D_PD_DL, 0x04, (n2d.pend_bat & 0xFFFFu) | (1u << 31), 0)) {
				return -2;
			}
			n2d.pend_bat = 0;
		}
		if (n2d_kick(N2D_PD_DL, 0x24, (n2d.pend_pit & 0xFFFFu) | (1u << 31), 1)) {
			return -3;
		}
		n2d.pend_pit = 0;
	}
	if (n2d.reload) {
		int r = n2d_add_apit(n2d.dummy_cnt);
		if (r < 0) {                                               /* DL did not go idle: give this reload up (Linux retries) */
			n2d.reload = 0;
			if (n2d.log_err++ < 10) {
				printf("NORD2 NET: dummy-PIT reload of %u failed (%d)\n", n2d.dummy_cnt, r);
			}
			return -4;
		}
		if (r > 0) {
			n2d.dummy_idx += n2d.dummy_cnt;
			if (n2d.dummy_idx >= N2D_DUMMY_MAX) {
				n2d.dummy_idx -= N2D_DUMMY_MAX;
			}
			n2d.dummy_cnt = 0;
			n2d.reload = 0;
			n2d.reloads++;
		}
	}
	return 0;
}

/* copy one buffer's data into the packet being assembled; the PIT address gives the offset (Linux assumes 0) */
static void
n2d_rx_copy(uint32_t off, uint32_t cap, uint64_t pit_pa, uint32_t len)
{
	uint64_t bpa = n2d_pa + off;
	uint32_t o = 0;

	if (pit_pa >= bpa && pit_pa - bpa < cap) {
		o = (uint32_t)(pit_pa - bpa);
	} else if (n2d.log_err++ < 10) {
		printf("NORD2 NET: rx data address %#llx outside its buffer %#llx, offset 0 assumed\n",
		    (unsigned long long)pit_pa, (unsigned long long)bpa);
	}
	if (len > cap - o || n2d.asm_len + len > sizeof(n2d.asm_buf)) {
		n2d.asm_bad = 1;
		n2d.rx_err++;
		return;
	}
	n2d_dma_inval(off + o, len);
	memcpy(n2d.asm_buf + n2d.asm_len, n2d_va + off + o, len);
	n2d.asm_len += len;
}

static void n2net_deliver(const uint8_t *p, uint32_t len, unsigned ch);

/* one payload PIT (normal or frag buffer) */
static void
n2d_rx_pd(uint32_t w0, uint32_t w1, uint32_t w2)
{
	uint32_t bid = (w0 >> 3) & 0x1FFFu, len = w0 >> 16;
	int frag = (w0 >> 2) & 1u, last = ((w0 >> 1) & 1u) == 0;
	uint64_t pa = ((uint64_t)w2 << 32) | w1;

	if (!frag) {
		if (bid >= N2D_BAT_N || !n2d.bat_busy[bid]) {
			n2d.rx_err++;
			n2d.asm_bad = 1;
			if (n2d.log_err++ < 10) {
				printf("NORD2 NET: rx bad BAT id %u (len %u)\n", bid, len);
			}
		} else {
			n2d.skb = (int)bid;
			n2d_rx_copy(N2D_OFF_RX + bid * N2D_BUF, N2D_PKT_SZ, pa, len);
			n2d.bat_busy[bid] = 0;
			n2d.pend_bat += n2d_recycle(0);
		}
	} else {
		n2d.rx_frag++;
		if (bid >= N2D_FRG_N || !n2d.frg_busy[bid]) {
			n2d.rx_err++;
			n2d.asm_bad = 1;
			if (n2d.log_err++ < 10) {
				printf("NORD2 NET: rx bad frag id %u (len %u)\n", bid, len);
			}
		} else {
			if (n2d.skb < 0) {                                  /* msg + frag without a head buffer */
				n2d.rx_err++;
				n2d.asm_bad = 1;
			} else {
				n2d_rx_copy(N2D_OFF_RXF + bid * N2D_BUF, N2D_FRG_SZ, pa, len);
			}
			n2d.frg_busy[bid] = 0;
			n2d.pend_frg += n2d_recycle(1);
		}
	}
	if (last) {
		if (n2d.skb >= 0 && !n2d.asm_bad && !n2d.cur_dp && n2d.asm_len != 0) {
			n2d.rx_pkts++;
			n2d.rx_bytes += n2d.asm_len;
			n2net_deliver(n2d.asm_buf, n2d.asm_len, n2d.cur_ch);
		} else {
			n2d.rx_drop++;
		}
		n2d.asm_len = 0;
		n2d.asm_bad = 0;
		n2d.skb = -1;
	}
}

/* dpmaifq_poll_rx_pit + dpmaif_rx_start; returns the number of PIT entries consumed */
static unsigned
n2d_rx_poll(unsigned budget)
{
	uint32_t wr, cnt, i;

	if (!n2d.started) {
		return 0;
	}
	if (n2d.pend_pit && n2d_rx_notify() < 0) {                           /* a notify failed last time */
		n2d.hw_err++;
		return 0;
	}
	wr = n2d_rd(N2D_AO_DL, 0x28) & 0xFFFFu;                              /* AO_DL_PIT_STA2[15:0]: HW write index */
	if (wr >= N2D_PIT_N) {
		if (n2d.log_err++ < 10) {
			printf("NORD2 NET: rx PIT write index %u out of range\n", wr);
		}
		return 0;
	}
	n2d.pit_wr = wr;
	cnt = (wr + N2D_PIT_N - n2d.pit_rd) % N2D_PIT_N;
	if (cnt > budget) {
		cnt = budget;
	}
	if (cnt == 0) {
		return 0;
	}
	if (n2d.pit_rd + cnt <= N2D_PIT_N) {
		n2d_dma_inval(N2D_OFF_PIT + n2d.pit_rd * 16u, cnt * 16u);
	} else {
		n2d_dma_inval(N2D_OFF_PIT + n2d.pit_rd * 16u, (N2D_PIT_N - n2d.pit_rd) * 16u);
		n2d_dma_inval(N2D_OFF_PIT, (n2d.pit_rd + cnt - N2D_PIT_N) * 16u);
	}
	for (i = 0; i < cnt; i++) {
		const uint8_t *e = n2d_va + N2D_OFF_PIT + n2d.pit_rd * 16u;
		uint32_t w0 = n2d_le32(e), w1 = n2d_le32(e + 4), w2 = n2d_le32(e + 8), w3 = n2d_le32(e + 12);

		if (n2d.log_pit < 16) {
			n2d.log_pit++;
			printf("NORD2 NET: pit[%u] %08x %08x %08x %08x\n", n2d.pit_rd, w0, w1, w2, w3);
		}
		if (w3 & (1u << 16)) {                                           /* ig: dummy PIT of the HW reorder workaround */
			n2d.reload = (int)((w3 >> 23) & 1u);
			n2d.dummy_cnt++;
			n2d.rx_dummy++;
		} else if (w0 & 1u) {                                            /* message PIT: channel (ccmni index), drop bit */
			n2d.cur_ch = (w0 >> 16) & 0xFFu;
			n2d.cur_csum = (w0 >> 2) & 3u;
			n2d.cur_dp = (w0 >> 31) & 1u;
			n2d.skb = -1;
			n2d.asm_len = 0;
			n2d.asm_bad = 0;
			n2d.rx_msg++;
		} else {
			n2d_rx_pd(w0, w1, w2);
		}
		n2d.pit_rd = (n2d.pit_rd + 1u) % N2D_PIT_N;
		n2d.pend_pit++;
		if ((n2d.pend_pit & 0x7Fu) == 0 && n2d_rx_notify() < 0) {
			n2d.hw_err++;
			return i + 1u;
		}
	}
	if (n2d_rx_notify() < 0) {
		n2d.hw_err++;
	}
	return cnt;
}

/* ---- UL ---- */

/* drv_dpmaif_ul_add_wcnt: DRB words */
static int
n2d_ul_add(unsigned q, uint32_t words)
{
	uint32_t off = 0xB0u + 4u * q;                                       /* UL_ADD_DESC_CHq */

	if (n2d_wait(N2D_PD_UL, off, 1u << 31, 0)) {
		return -1;
	}
	n2d_wr(N2D_PD_UL, off, (words & 0xFFFFu) | (1u << 31));
	if (n2d_wait(N2D_PD_UL, off, 1u << 31, 0)) {
		n2d.hw_err++;                                                    /* Linux only logs this */
	}
	return 0;
}

/* dpmaifq_poll_tx_drb: HW read index (words) of q0 */
static void
n2d_tx_reap(void)
{
	uint32_t r = ((n2d_rd(N2D_AO_UL, 0x00) >> 16) & 0xFFFFu) / 2u;

	if (r < N2D_DRB_N) {
		n2d.drb_rel = r;
	} else {
		n2d.hw_err++;
	}
}

/* dpmaif_tx_send_skb on q0: one msg DRB + one payload DRB; the packet is copied into the slot of the pair */
static int
n2d_tx(const uint8_t *pkt, uint32_t len, unsigned ch)
{
	uint32_t idx, off;
	uint64_t pa;
	uint8_t *d;

	if (!n2d.started) {
		return -1;
	}
	if (len == 0 || len > N2D_BUF) {
		n2d.tx_drop++;
		return -2;
	}
	n2d_tx_reap();
	if ((n2d.drb_rel + N2D_DRB_N - n2d.drb_wr - 1u) % N2D_DRB_N < 2u) {
		n2d.tx_busy++;
		return -3;
	}
	idx = n2d.drb_wr;
	off = N2D_OFF_TX + (idx / 2u) * N2D_BUF;
	memcpy(n2d_va + off, pkt, len);
	n2d_dma_clean(off, len);
	pa = n2d_pa + off;
	d = n2d_va + N2D_OFF_DRB + idx * 8u;
	n2d_st32(d, 1u | (1u << 2) | (len << 16));                           /* msg: dtyp MSG, c_bit 1, packet_len */
	n2d_st32(d + 4, (ch & 0xFFu) << 16);                                 /* count_l 0, channel_id, network_type 0, no csum offload */
	n2d_st32(d + 8, (((uint32_t)(pa >> 32) & 0xFFu) << 8) | (len << 16)); /* payload: dtyp PD, c_bit 0 (last), addr ext, len */
	n2d_st32(d + 12, (uint32_t)pa);
	n2d_dma_clean(N2D_OFF_DRB + idx * 8u, 16u);
	n2d.drb_wr = (idx + 2u) % N2D_DRB_N;
	if (n2d_ul_add(0, 2u * 2u)) {                                        /* 2 DRBs = 4 words */
		n2d.tx_err++;
		return -4;
	}
	n2d.tx_pkts++;
	n2d.tx_bytes += len;
	return 0;
}

/* ---- AT data-call info and the self-test packets ---- */

static struct {
	uint32_t ip, dns[2], mtu;
	int trans_id, ip_type, have_ip, have_dns, ifup;
	int state, attempts, dns_ok, icmp_ok;
	uint32_t t0, next, last_stats, dst;
	uint16_t sport, qid, icmp_id;
	uint32_t sent_ms;
	unsigned log_tx, log_rx;
} n2net = { .trans_id = -1, .mtu = 1500 };
static int n2net_ims_ifid = -1;                  /* 0277ar: the modem's IMS PDN interface (nord2_pdn_ims_iface) */
static int n2net_cid = 1;                        /* 0277at: the modem cid of the internet PDN (nord2_pdn_set_cid; 1 = 0277 boot script) */

/* 0277x: the second PDN (cid 2, nord2_bb_kpi.h nord2_pdn2_*): wanted while `apn[0]`, up after its +EIF ifup */
static struct {
	char apn[64];
	uint32_t ip, dns[2], mtu;
	int trans_id, ip_type, have_ip, up, confirm, dl_pdp, gone_id;
	int cid;                                           /* 0277z: the bridge's cid for it (2, 3, 5...), <= 0 unknown */
	unsigned ch;
	uint32_t rx, tx, rx_noif;
	uint32_t want_ip;                                  /* 0277bc: its address from +CGPADDR=<cid> (the +EIF interface must have it) */
} n2pdn2 = { .trans_id = -1, .mtu = 1500, .dl_pdp = -1, .gone_id = -1 };

/*
 * 0277bc: the interfaces other than cid 1's and the IMS PDN's, as their +EIF lines say (the modem's own XCAP PDN, 302 in q10dv/dw,
 * comes up by itself at any time): the second PDN (MMS) takes the one whose IPv4 address is its cid's (+CGPADDR), and before
 * that only an interface that came up after the bridge asked for it.
 */
#define N2NET_FOREIGN 6
static struct {
	int id, up;
	uint32_t ip, mtu;
} n2net_foreign[N2NET_FOREIGN];

static int
n2net_foreign_slot(int id)
{
	int i, freei = -1;

	for (i = 0; i < N2NET_FOREIGN; i++) {
		if (n2net_foreign[i].id == id && id != 0) {
			return i;
		}
		if (freei < 0 && (n2net_foreign[i].id == 0 || !n2net_foreign[i].up)) {
			freei = i;
		}
	}
	if (freei >= 0) {
		n2net_foreign[freei].id = id;
		n2net_foreign[freei].up = 0;
		n2net_foreign[freei].ip = 0;
		n2net_foreign[freei].mtu = 1500;
	}
	return freei;
}

/* bind the second PDN to foreign interface slot i (its address is the cid's) */
static void
n2pdn2_bind(int i, const char *why)
{
	n2pdn2.trans_id = n2net_foreign[i].id;
	n2pdn2.ip = n2net_foreign[i].ip;
	n2pdn2.have_ip = 1;
	n2pdn2.mtu = n2net_foreign[i].mtu;
	n2pdn2.ch = (unsigned)(n2pdn2.trans_id % 100);
	n2pdn2.up = 1;
	n2pdn2.confirm = 1;
	printf("NORD2 NET: second PDN (cid %d): modem interface %d ip %u.%u.%u.%u mtu %u -> ccmni%u (%s)\n", n2pdn2.cid, n2pdn2.trans_id,
	    (unsigned)(n2pdn2.ip >> 24), (unsigned)((n2pdn2.ip >> 16) & 255u), (unsigned)((n2pdn2.ip >> 8) & 255u),
	    (unsigned)(n2pdn2.ip & 255u), n2pdn2.mtu, n2pdn2.ch, why);
	sweet_bb_event(NORD2_BB_EV_PDN);
}

static int
n2net_pfx(const char *l, const char *p)
{
	while (*p) {
		if (*l++ != *p++) {
			return 0;
		}
	}
	return 1;
}

static int
n2net_streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		a++;
		b++;
	}
	return *a == *b;
}

static int
n2net_num(const char *s)
{
	int v = 0, neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9' && v < 100000000) {
		v = v * 10 + (*s++ - '0');
	}
	return neg ? -v : v;
}

/* "a.b.c.d" (a trailing ".m.m.m.m" subnet mask is ignored) */
static int
n2net_parse_ip(const char *s, uint32_t *out)
{
	uint32_t v = 0, o, i;

	for (i = 0; i < 4; i++) {
		if (*s < '0' || *s > '9') {
			return -1;
		}
		for (o = 0; *s >= '0' && *s <= '9'; s++) {
			o = o * 10u + (uint32_t)(*s - '0');
			if (o > 255u) {
				return -1;
			}
		}
		v = (v << 8) | o;
		if (i < 3) {
			if (*s != '.') {
				return -1;
			}
			s++;
		}
	}
	*out = v;
	return 0;
}

/* comma-separated fields, quotes removed, spaces trimmed */
#define N2NET_TOK 14
static int
n2net_tok(const char *s, char t[N2NET_TOK][40])
{
	int n = 0, q = 0;
	unsigned k = 0;

	while (*s == ' ') {
		s++;
	}
	for (;; s++) {
		char c = *s;
		if (c == '"') {
			q = !q;
			continue;
		}
		if (c == 0 || (c == ',' && !q)) {
			while (k > 0 && t[n][k - 1] == ' ') {
				k--;
			}
			t[n][k] = 0;
			n++;
			k = 0;
			if (c == 0 || n >= N2NET_TOK) {
				break;
			}
			while (s[1] == ' ') {
				s++;
			}
			continue;
		}
		if (k < 39) {
			t[n][k++] = c;
		}
	}
	return n;
}

#define N2NET_IP4(a) (unsigned)((a) >> 24), (unsigned)(((a) >> 16) & 255u), (unsigned)(((a) >> 8) & 255u), (unsigned)((a) & 255u)

/* one complete AT line from ttyC0: +EIF (modem NetAgent URC), +CGPADDR, +CGCONTRDP */
static void
n2net_at_line(const char *l)
{
	char t[N2NET_TOK][40];
	uint32_t a;
	int n, i;

	if (n2net_pfx(l, "+EIF:")) {
		n = n2net_tok(l + 5, t);
		/* 0277ar: the modem's IMS PDN (its interface id comes with +EIMSPDN: "notify", nord2_pdn_ims_iface) is never cid 1's or cid 2's */
		if (n >= 2 && n2net_ims_ifid >= 0 && n2net_num(t[0]) == n2net_ims_ifid) {
			static unsigned ims_lines;
			if (ims_lines++ < 16) {
				printf("NORD2 NET: +EIF %s %s of the IMS PDN's interface ignored\n", t[0], t[1]);
			}
			return;
		}
		/* 0277bc: every foreign interface's state, for the second PDN's address check */
		if (n >= 2 && n2net.trans_id >= 0 && n2net_num(t[0]) != n2net.trans_id) {
			int fi = n2net_foreign_slot(n2net_num(t[0]));
			if (fi >= 0) {
				if (n >= 4 && n2net_streq(t[1], "ifup")) {
					n2net_foreign[fi].up = 1;
					n2net_foreign[fi].ip = 0;
					for (i = 3; i < n; i++) {
						if (n2net_parse_ip(t[i], &a) == 0 && a != 0) {
							n2net_foreign[fi].ip = a;
							break;
						}
					}
				} else if (n >= 3 && n2net_streq(t[1], "mtu") && n2net_num(t[2]) >= 576 && n2net_num(t[2]) <= 1500) {
					n2net_foreign[fi].mtu = (uint32_t)n2net_num(t[2]);
				} else if (n2net_streq(t[1], "ifdown")) {
					n2net_foreign[fi].up = 0;
				}
			}
			/* an ifup whose address is not the one +CGPADDR gave the second PDN's cid is not the second PDN's */
			if (n >= 4 && n2net_streq(t[1], "ifup") && n2pdn2.apn[0] && n2pdn2.trans_id < 0 && fi >= 0 && n2pdn2.want_ip != 0 &&
			    n2net_foreign[fi].ip != n2pdn2.want_ip) {
				printf("NORD2 NET: +EIF %s ifup ip %u.%u.%u.%u is not the second PDN's (cid %d has %u.%u.%u.%u)\n", t[0],
				    N2NET_IP4(n2net_foreign[fi].ip), n2pdn2.cid, N2NET_IP4(n2pdn2.want_ip));
				return;
			}
		}
		/* 0277x: while cid 2 is wanted, an interface other than cid 1's is cid 2's (cid 1 keeps its state untouched) */
		if (n >= 2 && n2pdn2.apn[0] && n2net.trans_id >= 0 && n2net_num(t[0]) != n2net.trans_id &&
		    (n2pdn2.trans_id < 0 || n2net_num(t[0]) == n2pdn2.trans_id)) {
			if (n >= 4 && n2net_streq(t[1], "ifup")) {
				n2pdn2.trans_id = n2net_num(t[0]);
				n2pdn2.ip_type = n2net_num(t[2]);
				for (i = 3; i < n; i++) {
					if (n2net_parse_ip(t[i], &a) == 0 && a != 0) {
						n2pdn2.ip = a;
						n2pdn2.have_ip = 1;
						break;
					}
				}
				n2pdn2.ch = (unsigned)(n2pdn2.trans_id % 100);
				n2pdn2.up = n2pdn2.have_ip;
				n2pdn2.confirm = 1;
				printf("NORD2 NET: second PDN (cid 2): modem interface %d up ip %u.%u.%u.%u -> ccmni%u\n", n2pdn2.trans_id,
				    N2NET_IP4(n2pdn2.ip), n2pdn2.ch);
				sweet_bb_event(NORD2_BB_EV_PDN);
			} else if (n >= 3 && n2net_streq(t[1], "mtu")) {
				i = n2net_num(t[2]);
				if (i >= 576 && i <= 1500) {
					n2pdn2.mtu = (uint32_t)i;
				}
			} else if (n2net_streq(t[1], "ifdown")) {
				n2pdn2.up = 0;
				printf("NORD2 NET: second PDN (cid 2): modem interface %s down\n", t[0]);
				sweet_bb_event(NORD2_BB_EV_PDN);
			}
			return;
		}
		/* 0277aa: another PDN's interface (the modem's own IMS PDN, a late second PDN) while cid 1's is up is not cid 1's */
		if (n >= 2 && n2net.trans_id >= 0 && n2net.ifup && n2net_num(t[0]) != n2net.trans_id) {
			static unsigned foreign_lines;
			if (foreign_lines++ < 32) {
				printf("NORD2 NET: +EIF %s %s of interface %s ignored (cid 1 is interface %d)\n", t[0], t[1], t[0],
				    n2net.trans_id);
			}
			return;
		}
		if (n >= 2 && !n2pdn2.apn[0] && n2pdn2.gone_id >= 0 && n2net_num(t[0]) == n2pdn2.gone_id &&
		    n2net_num(t[0]) != n2net.trans_id) {
			printf("NORD2 NET: +EIF %s of the released second PDN's interface %d ignored\n", t[1], n2pdn2.gone_id);
			return;                                /* 0277x: a late line of the released cid 2 is not cid 1's */
		}
		if (n >= 4 && n2net_streq(t[1], "ifup")) {
			n2net.trans_id = n2net_num(t[0]);
			n2net.ip_type = n2net_num(t[2]);
			for (i = 3; i < n; i++) {
				if (n2net_parse_ip(t[i], &a) == 0 && a != 0) {
					n2net.ip = a;
					n2net.have_ip = 1;
					break;
				}
			}
			n2net.ifup = 1;
			printf("NORD2 NET: modem interface %d up (addr type %d) ip %u.%u.%u.%u -> ccmni%d\n", n2net.trans_id,
			    n2net.ip_type, N2NET_IP4(n2net.ip), n2net.trans_id >= 0 ? n2net.trans_id % 100 : 0);
		} else if (n >= 3 && n2net_streq(t[1], "mtu")) {
			i = n2net_num(t[2]);
			if (i >= 576 && i <= 1500) {
				n2net.mtu = (uint32_t)i;
			}
		} else if (n >= 2 && n2net_streq(t[1], "ifdown")) {
			n2net.ifup = 0;
			printf("NORD2 NET: modem interface %s down\n", t[0]);
		}
	} else if (n2net_pfx(l, "+CGPADDR:") && n2pdn2.apn[0] && n2pdn2.cid > 0 &&
	    n2net_num(l + 9 + (l[9] == ' ')) == n2pdn2.cid) {
		n = n2net_tok(l + 9, t);                       /* 0277x: cid 2 */
		if (n >= 2 && n2net_parse_ip(t[1], &a) == 0 && a != 0) {
			printf("NORD2 NET: cid %d address %u.%u.%u.%u (CGPADDR)\n", n2pdn2.cid, N2NET_IP4(a));
			n2pdn2.want_ip = a;                        /* 0277bc: the interface with this address is the second PDN's */
			if (n2pdn2.trans_id >= 0 && n2pdn2.have_ip && n2pdn2.ip != a) {
				int k, hit = -1;
				for (k = 0; k < N2NET_FOREIGN; k++) {
					if (n2net_foreign[k].up && n2net_foreign[k].ip == a && n2net_foreign[k].id != n2net_ims_ifid) {
						hit = k;
					}
				}
				printf("NORD2 NET: second PDN was bound to interface %d (%u.%u.%u.%u), not cid %d's address: %s\n",
				    n2pdn2.trans_id, N2NET_IP4(n2pdn2.ip), n2pdn2.cid, hit >= 0 ? "rebound" : "unbound, waiting for its +EIF");
				n2pdn2.trans_id = -1;
				n2pdn2.up = n2pdn2.have_ip = 0;
				if (hit >= 0) {
					n2pdn2_bind(hit, "its +CGPADDR address");
				} else {
					sweet_bb_event(NORD2_BB_EV_PDN);
				}
			} else if (n2pdn2.trans_id < 0) {
				int k;
				for (k = 0; k < N2NET_FOREIGN; k++) {
					if (n2net_foreign[k].up && n2net_foreign[k].ip == a && n2net_foreign[k].id != n2net_ims_ifid) {
						n2pdn2_bind(k, "its +CGPADDR address, +EIF came first");
						break;
					}
				}
				if (k == N2NET_FOREIGN) {
					printf("NORD2 NET: second PDN (cid %d): no interface with that address yet, waiting for its +EIF\n", n2pdn2.cid);
				}
			} else {
				printf("NORD2 NET: second PDN (cid %d): interface %d has that address (checked)\n", n2pdn2.cid, n2pdn2.trans_id);
			}
		}
	} else if (n2net_pfx(l, "+CGCONTRDP:") && n2pdn2.apn[0] && n2pdn2.cid > 0 &&
	    n2net_num(l + 11 + (l[11] == ' ')) == n2pdn2.cid) {
		n = n2net_tok(l + 11, t);                      /* 0277x: cid 2 */
		if (n >= 7) {
			for (i = 0; i < 2; i++) {
				if (n2net_parse_ip(t[5 + i], &a) == 0 && a != 0) {
					n2pdn2.dns[i] = a;
				}
			}
			if (n >= 12 && n2net_num(t[11]) >= 576 && n2net_num(t[11]) <= 1500) {
				n2pdn2.mtu = (uint32_t)n2net_num(t[11]);
			}
			printf("NORD2 NET: cid %d DNS %u.%u.%u.%u %u.%u.%u.%u mtu %u (CGCONTRDP)\n", n2pdn2.cid, N2NET_IP4(n2pdn2.dns[0]),
			    N2NET_IP4(n2pdn2.dns[1]), n2pdn2.mtu);
			sweet_bb_event(NORD2_BB_EV_PDN);
		}
	} else if (n2net_pfx(l, "+CGPADDR:")) {
		n = n2net_tok(l + 9, t);
		if (n >= 2 && n2net_num(t[0]) == n2net_cid && n2net_parse_ip(t[1], &a) == 0 && a != 0 && !n2net.have_ip) {
			n2net.ip = a;
			n2net.have_ip = 1;
			printf("NORD2 NET: cid 1 address %u.%u.%u.%u (CGPADDR)\n", N2NET_IP4(a));
		}
	} else if (n2net_pfx(l, "+CGCONTRDP:")) {
		n = n2net_tok(l + 11, t);
		if (n >= 7 && n2net_num(t[0]) == n2net_cid) {
			for (i = 0; i < 2; i++) {
				if (n2net_parse_ip(t[5 + i], &a) == 0 && a != 0) {
					n2net.dns[i] = a;
					n2net.have_dns = 1;
				}
			}
			if (n >= 12 && n2net_num(t[11]) >= 576 && n2net_num(t[11]) <= 1500) {
				n2net.mtu = (uint32_t)n2net_num(t[11]);
			}
			printf("NORD2 NET: cid 1 DNS %u.%u.%u.%u %u.%u.%u.%u mtu %u (CGCONTRDP)\n", N2NET_IP4(n2net.dns[0]),
			    N2NET_IP4(n2net.dns[1]), n2net.mtu);
		}
	}
}

static void
n2net_be16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

static void
n2net_be32(uint8_t *p, uint32_t v)
{
	n2net_be16(p, v >> 16);
	n2net_be16(p + 2, v);
}

static uint32_t
n2net_g16(const uint8_t *p)
{
	return ((uint32_t)p[0] << 8) | p[1];
}

static uint32_t
n2net_g32(const uint8_t *p)
{
	return (n2net_g16(p) << 16) | n2net_g16(p + 2);
}

static uint32_t
n2net_sum(const uint8_t *p, uint32_t n, uint32_t s)
{
	uint32_t i;

	for (i = 0; i + 1 < n; i += 2) {
		s += n2net_g16(p + i);
	}
	if (n & 1u) {
		s += (uint32_t)p[n - 1] << 8;
	}
	return s;
}

static uint32_t
n2net_fold(uint32_t s)
{
	while (s >> 16) {
		s = (s & 0xFFFFu) + (s >> 16);
	}
	return ~s & 0xFFFFu;
}

/* IPv4 header in front of a payload of plen bytes */
static uint32_t
n2net_ip(uint8_t *p, uint32_t src, uint32_t dst, uint8_t proto, uint32_t plen, uint32_t id)
{
	p[0] = 0x45;
	p[1] = 0;
	n2net_be16(p + 2, 20u + plen);
	n2net_be16(p + 4, id);
	n2net_be16(p + 6, 0x4000);                                           /* DF */
	p[8] = 64;
	p[9] = proto;
	n2net_be16(p + 10, 0);
	n2net_be32(p + 12, src);
	n2net_be32(p + 16, dst);
	n2net_be16(p + 10, n2net_fold(n2net_sum(p, 20, 0)));
	return 20u + plen;
}

/* UDP DNS query, type A, recursion desired */
static uint32_t
n2net_build_dns(uint8_t *p, uint32_t src, uint32_t dst, uint32_t sport, uint32_t qid, const char *name)
{
	uint8_t *u = p + 20, *d = u + 8, ph[12];
	uint32_t n = 12, c;
	const char *s = name;

	n2net_be16(d, qid);
	n2net_be16(d + 2, 0x0100);
	n2net_be16(d + 4, 1);
	n2net_be16(d + 6, 0);
	n2net_be16(d + 8, 0);
	n2net_be16(d + 10, 0);
	while (*s) {
		uint32_t k = 0;
		while (s[k] && s[k] != '.') {
			d[n + 1 + k] = (uint8_t)s[k];
			k++;
		}
		d[n] = (uint8_t)k;
		n += k + 1;
		s += k;
		if (*s == '.') {
			s++;
		}
	}
	d[n++] = 0;
	n2net_be16(d + n, 1);                                                /* QTYPE A */
	n2net_be16(d + n + 2, 1);                                            /* QCLASS IN */
	n += 4;
	n2net_be16(u, sport);
	n2net_be16(u + 2, 53);
	n2net_be16(u + 4, 8u + n);
	n2net_be16(u + 6, 0);
	n2net_be32(ph, src);
	n2net_be32(ph + 4, dst);
	ph[8] = 0;
	ph[9] = 17;
	n2net_be16(ph + 10, 8u + n);
	c = n2net_fold(n2net_sum(u, 8u + n, n2net_sum(ph, 12, 0)));
	n2net_be16(u + 6, c ? c : 0xFFFFu);
	return n2net_ip(p, src, dst, 17, 8u + n, qid);
}

/* ICMP echo request with a 24-byte payload */
static uint32_t
n2net_build_icmp(uint8_t *p, uint32_t src, uint32_t dst, uint32_t id, uint32_t seq)
{
	static const char pl[24] = "NORD2 DPMAIF ECHO TEST!";
	uint8_t *c = p + 20;
	uint32_t i;

	c[0] = 8;
	c[1] = 0;
	n2net_be16(c + 2, 0);
	n2net_be16(c + 4, id);
	n2net_be16(c + 6, seq);
	for (i = 0; i < sizeof(pl); i++) {
		c[8 + i] = (uint8_t)pl[i];
	}
	n2net_be16(c + 2, n2net_fold(n2net_sum(c, 8u + sizeof(pl), 0)));
	return n2net_ip(p, src, dst, 1, 8u + sizeof(pl), id ^ seq);
}

/* one-line packet summary for the first packets of each direction */
static void
n2net_describe(const char *tag, unsigned n, const uint8_t *p, uint32_t len, unsigned ch)
{
	if (len >= 20 && (p[0] >> 4) == 4) {
		uint32_t ihl = (p[0] & 15u) * 4u, s = n2net_g32(p + 12), d = n2net_g32(p + 16), a = 0, b = 0;
		if ((p[9] == 6 || p[9] == 17) && len >= ihl + 4) {
			a = n2net_g16(p + ihl);
			b = n2net_g16(p + ihl + 2);
		} else if (p[9] == 1 && len >= ihl + 2) {
			a = p[ihl];                                              /* ICMP type/code */
			b = p[ihl + 1];
		}
		printf("NORD2 NET: %s#%u ch%u len %u v4 proto %u %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n", tag, n, ch, len, p[9],
		    N2NET_IP4(s), a, N2NET_IP4(d), b);
	} else if (len >= 40 && (p[0] >> 4) == 6) {
		printf("NORD2 NET: %s#%u ch%u len %u v6 next %u hops %u\n", tag, n, ch, len, p[6], p[7]);
	} else {
		printf("NORD2 NET: %s#%u ch%u len %u: %02x %02x %02x %02x\n", tag, n, ch, len, len > 0 ? p[0] : 0, len > 1 ? p[1] : 0,
		    len > 2 ? p[2] : 0, len > 3 ? p[3] : 0);
	}
}

/* skip a DNS name (labels or a compression pointer); 0 on malformed input */
static uint32_t
n2net_dns_skip(const uint8_t *d, uint32_t n, uint32_t o)
{
	while (o < n) {
		if (d[o] == 0) {
			return o + 1;
		}
		if ((d[o] & 0xC0u) == 0xC0u) {
			return o + 2 <= n ? o + 2 : 0;
		}
		o += 1u + d[o];
	}
	return 0;
}

/* consume the replies to the self-test; 1 = DNS reply, 2 = echo reply, 0 = not ours */
static int
n2net_selftest_rx(const uint8_t *p, uint32_t len)
{
	uint32_t ihl, src, rtt;

	if (len < 28 || (p[0] >> 4) != 4) {
		return 0;
	}
	ihl = (p[0] & 15u) * 4u;
	src = n2net_g32(p + 12);
	if (len < ihl + 8 || n2net_g32(p + 16) != n2net.ip) {
		return 0;
	}
	rtt = n2net_now_ms() - n2net.sent_ms;
	if (p[9] == 17 && n2net_g16(p + ihl) == 53 && n2net.sport != 0 && n2net_g16(p + ihl + 2) == n2net.sport) {
		const uint8_t *d = p + ihl + 8;
		uint32_t n = len - ihl - 8, o, k, an, a = 0;
		if (n < 12) {
			return 1;
		}
		an = n2net_g16(d + 6);
		o = n2net_dns_skip(d, n, 12);
		o = o && o + 4 <= n ? o + 4 : 0;
		for (k = 0; o && k < an; k++) {
			uint32_t t, rl;
			o = n2net_dns_skip(d, n, o);
			if (!o || o + 10 > n) {
				break;
			}
			t = n2net_g16(d + o);
			rl = n2net_g16(d + o + 8);
			if (o + 10 + rl > n) {
				break;
			}
			if (t == 1 && rl == 4) {
				a = n2net_g32(d + o + 10);
				break;
			}
			o += 10 + rl;
		}
		n2net.dns_ok++;
		printf("NORD2 NET: DNS REPLY from %u.%u.%u.%u id 0x%04x rcode %u answers %u A %u.%u.%u.%u rtt %u ms -> DATA PATH UP\n",
		    N2NET_IP4(src), n2net_g16(d), n2net_g16(d + 2) & 15u, an, N2NET_IP4(a), rtt);
		return 1;
	}
	if (p[9] == 1 && p[ihl] == 0 && n2net.icmp_id != 0 && n2net_g16(p + ihl + 4) == n2net.icmp_id) {
		n2net.icmp_ok++;
		printf("NORD2 NET: ICMP ECHO REPLY from %u.%u.%u.%u seq %u ttl %u rtt %u ms\n", N2NET_IP4(src), n2net_g16(p + ihl + 6),
		    p[8], rtt);
		return 2;
	}
	return 0;
}
/* SWEET_NET_CORE_END */

/* ---- N2NET glue: DMA region, pdp_ip0, self-test, hooks of the modem thread ---- */
static int n2net_prepared;
static ifnet_t n2net_ifp;                          /* pdp_ip0 */
/*
 * 0277l: with the baseband bridge owning cellular data (nord2_pdn_owner, nord2_bb_kpi.h), pdp_ip0..pdp_ip4 exist from the bridge's
 * publication on, like the interfaces of iOS's PDP manager, without an address: CommCenter's WDS call picks one (its mux id), configd
 * configures it. The downlink goes to n2net_dl_sel (-1 until a call), the uplink comes from any of them; no kernel address or route.
 */
#define N2NET_IFS 5
#define N2NET_APN "internet.it"                    /* the APN of the boot script's AT+CGDCONT=1 */
static ifnet_t n2net_ifps[N2NET_IFS];
static volatile int n2net_dl_sel;                  /* legacy: 0 (pdp_ip0); owner mode: -1 until CommCenter's call */
int nord2_pdn_owner;
static uint32_t n2net_pdn_events, n2net_seen_ip;
static lck_grp_t *n2net_grp;
static lck_mtx_t *n2net_lck;
static mbuf_t n2net_qh, n2net_qt;                  /* packets from the IP stack, sent by the modem thread */
static volatile unsigned n2net_qlen;
static unsigned n2net_ch;                          /* UL channel id = ccmni index of the PDN */
static uint8_t n2net_txbuf[N2D_BUF], n2net_stbuf[256];
static uint32_t n2net_q_drop, n2net_rx_noif, n2net_rx_nombuf;
static uint32_t n2net_if_ip;                       /* address configured on pdp_ip0 (0: none) */
static int n2net_r_scoped = -1, n2net_r_def = -1;  /* last results of the default route requests (0 = ours is installed) */
static uint32_t n2net_def_next, n2net_readdr, n2net_def_adds, n2net_pkts_seen;
static char n2net_sysctl_pdp[160] = "down";

/* sysctl net.nord2.pdp: "pdp_ip0 ip a.b.c.d dns a.b.c.d a.b.c.d mtu N ch N" for a userspace helper (configd owns the resolver) */
SYSCTL_NODE(_net, OID_AUTO, nord2, CTLFLAG_RW | CTLFLAG_LOCKED, 0, "Nord2 cellular data");
SYSCTL_UINT(_kern, OID_AUTO, n2sim_fix, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_sim_fix, 0, "0710: SIM/PIN fixes bit mask, 0 = 0277");
SYSCTL_UINT(_kern, OID_AUTO, n2sim_state, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_sim_state, 0, "0710: raw | settled<<8 | shown<<16 | identity<<24");
SYSCTL_UINT(_kern, OID_AUTO, n2sim_apn, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_sim_apn, 0, "0711: 1 = the SIM's APN, 0 = internet.it for every SIM");
SYSCTL_UINT(_kern, OID_AUTO, n2nr, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_nr, 0, "0712: 1 = NR (5G NSA) reporting from the modem");
SYSCTL_UINT(_kern, OID_AUTO, n2nr_state, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_nr_state, 0, "0712: ENRCABAND state | band<<8 | ECAPABILITY<<24");
/* 0713: baseband link word and the userland helper's step log (n2simcache) */
SYSCTL_UINT(_kern, OID_AUTO, n2bb_link, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_bb_link, 0, "0713: published | clients<<8 | sahara<<16 | power-ons<<24");
static int
sysctl_n2sim_helper SYSCTL_HANDLER_ARGS
{
#pragma unused(arg1, arg2)
	unsigned v = nord2_sim_helper;
	int error = sysctl_handle_int(oidp, &v, 0, req);
	if (!error && req->newptr != USER_ADDR_NULL) {
		nord2_sim_helper = v;
		printf("NORD2 SIM: helper step 0x%08x (n2simcache)\n", v);
	}
	return error;
}
SYSCTL_PROC(_kern, OID_AUTO, n2sim_helper, CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_LOCKED, 0, 0, sysctl_n2sim_helper, "IU",
    "0713: userland helper step codes, printed");
SYSCTL_UINT(_kern, OID_AUTO, n2nv_src, CTLFLAG_RD | CTLFLAG_LOCKED, &nord2_nv_src, 0, "0715: modem NVRAM blob source: 1 raw userdata+64GiB, 2 rootfs file, +0x100 file invalid");
SYSCTL_UINT(_kern, OID_AUTO, n2nr_ui, CTLFLAG_RW | CTLFLAG_LOCKED, &nord2_nr_ui, 0, "0714: 1 = NR5G next to LTE in the NAS serving system while the NR leg is up (experiment)");
SYSCTL_STRING(_net_nord2, OID_AUTO, pdp, CTLFLAG_RD | CTLFLAG_LOCKED, n2net_sysctl_pdp, sizeof(n2net_sysctl_pdp), "PDN of cid 1");

/* one DL packet from the DPMAIF (called by n2d_rx_poll in the modem thread) */
static void
n2net_deliver(const uint8_t *p, uint32_t len, unsigned ch)
{
	struct ifnet_stat_increment_param st;
	mbuf_t m = NULL;

	if (n2net.log_rx < 8) {
		n2net.log_rx++;
		n2net_describe("rx", n2net.log_rx, p, len, ch);
	}
	if (n2net_selftest_rx(p, len)) {
		return;                                    /* the self-test replies are not handed to the stack */
	}
	ifnet_t ifp = n2net_ifp;
	if (nord2_pdn_owner) {                         /* 0277l: the interface of CommCenter's call, if any */
		int sel = n2net_dl_sel;
		ifp = sel >= 0 && sel < N2NET_IFS ? n2net_ifps[sel] : NULL;
		if (n2pdn2.up && ch == n2pdn2.ch && ch != n2net_ch) {     /* 0277x: the second PDN's channel */
			sel = n2pdn2.dl_pdp;
			ifp = sel >= 0 && sel < N2NET_IFS ? n2net_ifps[sel] : NULL;
			if (ifp == NULL) {
				n2pdn2.rx_noif++;
				return;
			}
			n2pdn2.rx++;
		}
	}
	if (ifp == NULL) {
		n2net_rx_noif++;
		return;
	}
	if (len > 2048 || mbuf_gethdr(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) != 0 ||
	    mbuf_mclget(MBUF_DONTWAIT, MBUF_TYPE_DATA, &m) != 0 || !(mbuf_flags(m) & MBUF_EXT) || mbuf_maxlen(m) < len) {
		if (m != NULL) {
			mbuf_freem(m);
		}
		n2net_rx_nombuf++;
		ifnet_stat_increment_in(ifp, 0, 0, 1);
		return;
	}
	memcpy(mbuf_data(m), p, len);
	mbuf_setlen(m, len);
	mbuf_pkthdr_setlen(m, len);
	mbuf_pkthdr_setrcvif(m, ifp);
	mbuf_pkthdr_setheader(m, mbuf_data(m));        /* raw IP: no link header */
	bzero(&st, sizeof(st));
	st.packets_in = 1;
	st.bytes_in = len;
	ifnet_input(ifp, m, &st);
}

/* legacy-model output: queue, the modem thread sends (DPMAIF registers are only touched from that thread) */
static errno_t
n2net_if_output(ifnet_t ifp, mbuf_t m)
{
	if (n2net_lck == NULL) {                           /* 0277l: interfaces may exist before the DMA region */
		mbuf_freem(m);
		n2net_q_drop++;
		return 0;
	}
	lck_mtx_lock(n2net_lck);
	if (n2net_qlen >= 64 || !n2d.started) {
		lck_mtx_unlock(n2net_lck);
		mbuf_freem(m);
		n2net_q_drop++;
		ifnet_stat_increment_out(ifp, 0, 0, 1);
		return 0;
	}
	mbuf_setnextpkt(m, NULL);
	mbuf_pkthdr_setrcvif(m, ifp);                      /* 0277x: the output interface picks the UL channel (n2net_tx_drain) */
	if (n2net_qt != NULL) {
		mbuf_setnextpkt(n2net_qt, m);
	} else {
		n2net_qh = m;
	}
	n2net_qt = m;
	n2net_qlen++;
	lck_mtx_unlock(n2net_lck);
	lck_mtx_lock(&sweet_n2at_mtx);                     /* p2: wake the steady service loop at once */
	sweet_svc_kick = 1;
	wakeup(&sweet_svc_kick);
	lck_mtx_unlock(&sweet_n2at_mtx);
	return 0;
}

static void
n2net_tx_drain(void)
{
	mbuf_t m, next;

	if (n2net_qlen == 0 || n2net_lck == NULL) {
		return;
	}
	lck_mtx_lock(n2net_lck);
	m = n2net_qh;
	n2net_qh = n2net_qt = NULL;
	n2net_qlen = 0;
	lck_mtx_unlock(n2net_lck);
	for (; m != NULL; m = next) {
		size_t len = mbuf_pkthdr_len(m);
		unsigned ch = n2net_ch;
		next = mbuf_nextpkt(m);
		mbuf_setnextpkt(m, NULL);
		if (n2pdn2.up && n2pdn2.dl_pdp >= 0 && n2pdn2.dl_pdp < N2NET_IFS && n2net_ifps[n2pdn2.dl_pdp] != NULL &&
		    mbuf_pkthdr_rcvif(m) == n2net_ifps[n2pdn2.dl_pdp] && n2pdn2.dl_pdp != n2net_dl_sel) {
			ch = n2pdn2.ch;                        /* 0277x: the second PDN's interface */
			n2pdn2.tx++;
		}
		if (len > 0 && len <= sizeof(n2net_txbuf) && mbuf_copydata(m, 0, len, n2net_txbuf) == 0 &&
		    n2d_tx(n2net_txbuf, (uint32_t)len, ch) == 0) {
			if (n2net.log_tx < 8) {
				n2net.log_tx++;
				n2net_describe("tx", n2net.log_tx, n2net_txbuf, (uint32_t)len, n2net_ch);
			}
			ifnet_stat_increment_out(n2net_ifp, 1, (u_int32_t)len, 0);
		} else {
			ifnet_stat_increment_out(n2net_ifp, 0, 0, 1);
		}
		mbuf_freem(m);
	}
}

static errno_t
n2net_demux(__unused ifnet_t ifp, mbuf_t m, __unused char *hdr, protocol_family_t *pf)
{
	while (m != NULL && mbuf_len(m) < 1) {
		m = mbuf_next(m);
	}
	if (m == NULL) {
		return ENOENT;
	}
	switch (*(const uint8_t *)mbuf_data(m) >> 4) {
	case 4:
		*pf = PF_INET;
		return 0;
	case 6:
		*pf = PF_INET6;
		return 0;
	default:
		return ENOENT;
	}
}

static errno_t
n2net_add_proto(__unused ifnet_t ifp, protocol_family_t pf, __unused const struct ifnet_demux_desc *d, __unused u_int32_t n)
{
	return pf == PF_INET ? 0 : ENOPROTOOPT;
}

static errno_t
n2net_del_proto(__unused ifnet_t ifp, __unused protocol_family_t pf)
{
	return 0;
}

static errno_t
n2net_ioctl(ifnet_t ifp, unsigned long cmd, void *data)
{
	switch (cmd) {
	case SIOCSIFMTU:
		if (((struct ifreq *)data)->ifr_mtu < 576 || ((struct ifreq *)data)->ifr_mtu > 1500) {
			return EINVAL;
		}
		return ifnet_set_mtu(ifp, (u_int32_t)((struct ifreq *)data)->ifr_mtu);
	case SIOCSIFADDR:
	case SIOCAIFADDR:
	case SIOCSIFDSTADDR:
	case SIOCSIFFLAGS:
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static void
n2net_detached(__unused ifnet_t ifp)
{
}

static errno_t
n2net_proto_input(ifnet_t ifp, protocol_family_t pf, mbuf_t m, __unused char *hdr)
{
	mbuf_pkthdr_setrcvif(m, ifp);
	if (proto_input(pf, m) != 0) {
		mbuf_freem(m);
	}
	return 0;
}

static errno_t
n2net_proto_preout(__unused ifnet_t ifp, protocol_family_t pf, __unused mbuf_t *m, __unused const struct sockaddr *dest,
    __unused void *route, char *frame_type, __unused char *lladdr)
{
	*(protocol_family_t *)(void *)frame_type = pf;
	return 0;
}

/* plumber for (PF_INET, IFNET_FAMILY_CELLULAR): in_domifattach() needs one; only our interface is accepted */
static errno_t
n2net_plumb(ifnet_t ifp, protocol_family_t pf)
{
	struct ifnet_attach_proto_param pr;

	unsigned i;
	for (i = 0; i < N2NET_IFS && ifp != n2net_ifps[i]; i++) {
	}
	if ((i == N2NET_IFS && ifp != n2net_ifp) || pf != PF_INET) {
		return ENXIO;
	}
	bzero(&pr, sizeof(pr));
	pr.input = n2net_proto_input;
	pr.pre_output = n2net_proto_preout;
	return ifnet_attach_protocol(ifp, pf, &pr);
}

static void
n2net_unplumb(ifnet_t ifp, protocol_family_t pf)
{
	(void)ifnet_detach_protocol(ifp, pf);
}

/* SIOCAIFADDR (point-to-point, dst = own address like iOS cellular, /32) or SIOCDIFADDR of ip on pdp_ip0 */
static int
n2net_addr(int add, uint32_t ip)
{
	socket_t so = NULL;
	struct sockaddr_in *a;
	int err;

	if (sock_socket(PF_INET, SOCK_DGRAM, 0, NULL, NULL, &so) != 0) {
		return -1;
	}
	if (add) {
		struct ifaliasreq ifra;
		bzero(&ifra, sizeof(ifra));
		snprintf(ifra.ifra_name, sizeof(ifra.ifra_name), "%s%u", ifnet_name(n2net_ifp), ifnet_unit(n2net_ifp));
		a = (struct sockaddr_in *)(void *)&ifra.ifra_addr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(ip);
		a = (struct sockaddr_in *)(void *)&ifra.ifra_broadaddr;      /* point-to-point destination */
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(ip);
		a = (struct sockaddr_in *)(void *)&ifra.ifra_mask;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(0xFFFFFFFFu);
		err = sock_ioctl(so, SIOCAIFADDR, &ifra);
	} else {
		struct ifreq ifr;
		bzero(&ifr, sizeof(ifr));
		snprintf(ifr.ifr_name, sizeof(ifr.ifr_name), "%s%u", ifnet_name(n2net_ifp), ifnet_unit(n2net_ifp));
		a = (struct sockaddr_in *)(void *)&ifr.ifr_addr;
		a->sin_len = sizeof(*a);
		a->sin_family = AF_INET;
		a->sin_addr.s_addr = htonl(ip);
		err = sock_ioctl(so, SIOCDIFADDR, &ifr);
	}
	sock_close(so);
	return err;
}

/* the unscoped default route belongs to pdp_ip0 (1), to another interface (0), or does not exist (-1) */
static int
n2net_default_is_ours(void)
{
	struct sockaddr_in dst;
	struct rtentry *rt;
	int r;

	bzero(&dst, sizeof(dst));
	dst.sin_len = sizeof(dst);
	dst.sin_family = AF_INET;
	rt = rtalloc1((struct sockaddr *)&dst, 0, 0);
	if (rt == NULL) {
		return -1;
	}
	r = rt->rt_ifp == n2net_ifp;
	rtfree(rt);
	return r;
}

/* interface default routes through ip: scoped to pdp_ip0 always, global only while no other default exists (Wi-Fi keeps it) */
static void
n2net_routes(int req, uint32_t ip)
{
	struct sockaddr_in dst, msk, gw;

	bzero(&dst, sizeof(dst));
	bzero(&msk, sizeof(msk));
	bzero(&gw, sizeof(gw));
	dst.sin_len = msk.sin_len = gw.sin_len = sizeof(struct sockaddr_in);
	dst.sin_family = msk.sin_family = gw.sin_family = AF_INET;
	gw.sin_addr.s_addr = htonl(ip);                            /* interface route: no RTF_GATEWAY, ifa found by its address */
	if (req == RTM_ADD) {
		n2net_r_scoped = rtrequest_scoped(RTM_ADD, (struct sockaddr *)&dst, (struct sockaddr *)&gw, (struct sockaddr *)&msk,
		    RTF_UP | RTF_STATIC, NULL, ifnet_index(n2net_ifp));
		n2net_r_def = (nord2_net & 8) ? -1 : rtrequest(RTM_ADD, (struct sockaddr *)&dst, (struct sockaddr *)&gw,
		    (struct sockaddr *)&msk, RTF_UP | RTF_STATIC, NULL);
		return;
	}
	(void)rtrequest_scoped(RTM_DELETE, (struct sockaddr *)&dst, (struct sockaddr *)&gw, (struct sockaddr *)&msk,
	    RTF_UP | RTF_STATIC, NULL, ifnet_index(n2net_ifp));
	if (n2net_default_is_ours() == 1) {                        /* never delete a default of another interface */
		(void)rtrequest(RTM_DELETE, (struct sockaddr *)&dst, (struct sockaddr *)&gw, (struct sockaddr *)&msk, RTF_UP | RTF_STATIC,
		    NULL);
	}
	n2net_r_scoped = n2net_r_def = -1;
}

static void
n2net_publish(void)
{
	if (nord2_pdn_owner) {
		snprintf(n2net_sysctl_pdp, sizeof(n2net_sysctl_pdp), "bridge pdp_ip%d pdn %u.%u.%u.%u dns %u.%u.%u.%u %u.%u.%u.%u mtu %u ch %u",
		    n2net_dl_sel, N2NET_IP4(n2net.ip), N2NET_IP4(n2net.dns[0]), N2NET_IP4(n2net.dns[1]), n2net.mtu, n2net_ch);
		return;
	}
	snprintf(n2net_sysctl_pdp, sizeof(n2net_sysctl_pdp), "pdp_ip0 ip %u.%u.%u.%u dns %u.%u.%u.%u %u.%u.%u.%u mtu %u ch %u",
	    N2NET_IP4(n2net_if_ip), N2NET_IP4(n2net.dns[0]), N2NET_IP4(n2net.dns[1]), n2net.mtu, n2net_ch);
}

/* 0277l: one pdp_ip<unit> (created once, never destroyed), attached, plumbed for IPv4, up, without an address */
static int n2net_plumber_done;
static const char *const n2net_uids[N2NET_IFS] = { "nord2-ccmni0", "nord2-pdp1", "nord2-pdp2", "nord2-pdp3", "nord2-pdp4" };

static ifnet_t
n2net_if_alloc(unsigned unit, errno_t *e_alloc, errno_t *e_att)
{
	struct ifnet_init_eparams init;
	ifnet_t ifp = NULL;

	*e_att = -1;
	bzero(&init, sizeof(init));
	init.ver = IFNET_INIT_CURRENT_VERSION;
	init.len = sizeof(init);
	init.flags = IFNET_INIT_LEGACY | IFNET_INIT_NX_NOAUTO;
	init.uniqueid = n2net_uids[unit];
	init.uniqueid_len = (uint32_t)strlen(n2net_uids[unit]);
	init.name = "pdp_ip";
	init.unit = unit;
	init.family = IFNET_FAMILY_CELLULAR;
	init.type = IFT_CELLULAR;
	init.output = n2net_if_output;
	init.demux = n2net_demux;
	init.add_proto = n2net_add_proto;
	init.del_proto = n2net_del_proto;
	init.ioctl = n2net_ioctl;
	init.detach = n2net_detached;
	*e_alloc = ifnet_allocate_extended(&init, &ifp);
	if (*e_alloc == 0) {
		ifnet_set_mtu(ifp, n2net.mtu);
		ifnet_set_hdrlen(ifp, 0);
		ifnet_set_addrlen(ifp, 0);
		ifnet_set_baudrate(ifp, 150000000);
		ifnet_set_flags(ifp, IFF_POINTOPOINT | IFF_NOARP, IFF_POINTOPOINT | IFF_NOARP | IFF_BROADCAST | IFF_MULTICAST);
		*e_att = ifnet_attach(ifp, NULL);
	}
	if (*e_alloc != 0 || *e_att != 0) {
		if (ifp != NULL) {
			ifnet_release(ifp);
		}
		return NULL;
	}
	n2net_ifps[unit] = ifp;
	if (unit == 0) {
		n2net_ifp = ifp;
	}
	if (!n2net_plumber_done) {
		n2net_plumber_done = 1;
		(void)proto_register_plumber(PF_INET, IFNET_FAMILY_CELLULAR, n2net_plumb, n2net_unplumb);
	}
	(void)proto_plumb(PF_INET, ifp);
	ifnet_set_flags(ifp, IFF_UP | IFF_RUNNING, IFF_UP | IFF_RUNNING);
	return ifp;
}

/* 0277l KPI: pdp_ip0..4 for CommCenter (bridge mode), from the bridge's publication thread call */
int
nord2_pdn_ifaces(void)
{
	errno_t ea = 0, et = 0;
	unsigned made = 0, have = 0;

	static volatile UInt32 busy;

	if (!nord2_pdn_owner) {
		return ENOTSUP;
	}
	if (!OSCompareAndSwap(0, 1, &busy)) {                  /* the bridge's thread call and the modem thread may both get here */
		return EBUSY;
	}
	if (n2net_lck == NULL) {
		n2net_grp = lck_grp_alloc_init("nord2_net", LCK_GRP_ATTR_NULL);
		n2net_lck = lck_mtx_alloc_init(n2net_grp, LCK_ATTR_NULL);
	}
	for (unsigned u = 0; u < N2NET_IFS; u++) {
		if (n2net_ifps[u] != NULL) {
			have++;
		} else if (n2net_if_alloc(u, &ea, &et) != NULL) {
			made++;
		}
	}
	if (made) {
		printf("NORD2 NET: bridge mode: pdp_ip0..%u for CommCenter (%u made, %u there; last allocate %d attach %d), no kernel address"
		    " or route\n", N2NET_IFS - 1, made, have, ea, et);
	}
	busy = 0;
	return made + have == N2NET_IFS ? 0 : EIO;
}

/* 0277at KPI: the internet PDN is modem cid `cid` (from +CGEV: ME PDN ACT after the bridge's AT+EAPNACT default): its +CGPADDR /
 * +CGCONTRDP lines are cid 1's (the kernel's name for the internet PDN). Modem service thread. */
void
nord2_pdn_set_cid(int cid)
{
	if (cid != n2net_cid) {
		printf("NORD2 NET: the internet PDN is modem cid %d (was %d)\n", cid, n2net_cid);
	}
	n2net_cid = cid;
}

/* 0277ar KPI: the modem's IMS PDN uses interface `ifid` (from +EIMSPDN: "notify"): its +EIF lines are not cid 1's; if cid 1 had
 * already been bound to it (the IMS PDN came up first), cid 1 is unbound so the internet PDN's interface is taken when it comes.
 * Modem service thread (the bridge calls it from its URC hook). */
void
nord2_pdn_ims_iface(int ifid)
{
	n2net_ims_ifid = ifid;
	printf("NORD2 NET: interface %d is the modem's IMS PDN (not cid 1's)\n", ifid);
	if (n2net.trans_id == ifid) {
		printf("NORD2 NET: cid 1 was bound to the IMS PDN's interface %d: unbound, waiting for the internet PDN\n", ifid);
		n2net.trans_id = -1;
		n2net.ifup = 0;
		n2net.have_ip = 0;
		n2net.ip = 0;
		sweet_bb_event(NORD2_BB_EV_PDN);
	}
}

/* 0277l KPI: the PDN as the bridge answers WDS with it */
int
nord2_pdn_get(struct nord2_pdn_info *out)
{
	out->up = n2net.have_ip && n2net.ip != 0 && n2d.started && !sweet_md_dead;
	out->ip = n2net.ip;
	out->dns[0] = n2net.dns[0];
	out->dns[1] = n2net.dns[1];
	out->mtu = n2net.mtu;
	out->ch = n2net_ch;
	out->apn = N2NET_APN;
	return out->up ? 0 : ENXIO;
}

/* 0277l KPI: the downlink goes to pdp_ip<pdp> (CommCenter's call), -1 nowhere */
void
nord2_pdn_route(int pdp)
{
	int old = n2net_dl_sel;
	n2net_dl_sel = pdp >= 0 && pdp < N2NET_IFS ? pdp : -1;
	if (old != n2net_dl_sel) {
		printf("NORD2 NET: downlink -> %s%d (was %d)\n", n2net_dl_sel >= 0 ? "pdp_ip" : "nowhere ", n2net_dl_sel, old);
	}
}

/* 0277x KPI: the second PDN (cid 2) */
void
nord2_pdn2_expect(const char *apn, int cid)
{
	if (apn == NULL || apn[0] == 0) {
		if (n2pdn2.apn[0]) {
			printf("NORD2 NET: second PDN (cid 2) released (rx %u tx %u, rx without interface %u)\n", n2pdn2.rx, n2pdn2.tx,
			    n2pdn2.rx_noif);
		}
		n2pdn2.apn[0] = 0;
		n2pdn2.up = n2pdn2.have_ip = n2pdn2.confirm = 0;
		if (n2pdn2.trans_id >= 0) {
			n2pdn2.gone_id = n2pdn2.trans_id;
		}
		n2pdn2.trans_id = -1;
		n2pdn2.ip = n2pdn2.dns[0] = n2pdn2.dns[1] = 0;
		n2pdn2.dl_pdp = -1;
		n2pdn2.want_ip = 0;
		return;
	}
	/* 0277z: the bridge's next variant (another cid); 0277bc: or its cid became known (+CGEV), maybe after its +EIF bound it
	 * (the binding is checked again at the cid's +CGPADDR) */
	if (cid != n2pdn2.cid && (!n2pdn2.up || n2pdn2.cid <= 0)) {
		n2pdn2.cid = cid;
		if (!n2pdn2.up) {
			n2pdn2.have_ip = 0;
			n2pdn2.ip = 0;
		}
		n2pdn2.dns[0] = n2pdn2.dns[1] = 0;
		n2pdn2.want_ip = 0;
		printf("NORD2 NET: second PDN now expected on cid %d%s\n", cid, n2pdn2.up ? " (its interface is already up)" : "");
	}
	if (strncmp(n2pdn2.apn, apn, sizeof(n2pdn2.apn)) != 0) {
		strlcpy(n2pdn2.apn, apn, sizeof(n2pdn2.apn));
		n2pdn2.up = n2pdn2.have_ip = 0;
		n2pdn2.trans_id = -1;
		n2pdn2.ip = n2pdn2.dns[0] = n2pdn2.dns[1] = 0;
		n2pdn2.mtu = 1500;
		n2pdn2.want_ip = 0;
		printf("NORD2 NET: second PDN (cid %d) wanted for APN \"%s\"\n", cid, n2pdn2.apn);
	}
}

int
nord2_pdn2_get(struct nord2_pdn_info *out)
{
	out->up = n2pdn2.apn[0] && n2pdn2.up && n2pdn2.have_ip && n2pdn2.ip != 0 && n2d.started && !sweet_md_dead;
	out->ip = n2pdn2.ip;
	out->dns[0] = n2pdn2.dns[0];
	out->dns[1] = n2pdn2.dns[1];
	if (out->dns[0] == 0 && out->up && n2net.dns[0] != 0) {   /* 0277z: no +CGCONTRDP for it (yet): cid 1's resolvers */
		out->dns[0] = n2net.dns[0];
		out->dns[1] = n2net.dns[1];
	}
	out->mtu = n2pdn2.mtu;
	out->ch = n2pdn2.ch;
	out->apn = n2pdn2.apn;
	return out->up ? 0 : ENXIO;
}

void
nord2_pdn2_route(int pdp)
{
	int old = n2pdn2.dl_pdp;
	n2pdn2.dl_pdp = pdp >= 0 && pdp < N2NET_IFS ? pdp : -1;
	if (old != n2pdn2.dl_pdp) {
		printf("NORD2 NET: second PDN downlink -> %s%d (was %d)\n", n2pdn2.dl_pdp >= 0 ? "pdp_ip" : "nowhere ", n2pdn2.dl_pdp, old);
	}
}

/* pdp_ip0 (created once, never destroyed) with the PDN address and routes; 0277l bridge mode: the interfaces only */
static void
n2net_if_up(void)
{
	struct ifnet_init_eparams init;
	ifnet_t ifp = NULL;
	errno_t e_alloc, e_att = -1, e_reg, e_pl, e_addr;

	if (nord2_pdn_owner) {
		(void)nord2_pdn_ifaces();
		n2net_publish();
		/* 0277n: the DNS comes from AT+CGCONTRDP=1, which follows the address ("NORD2 NET: cid 1 DNS ... (CGCONTRDP)") */
		if (n2net.have_dns) {
			printf("NORD2 NET: PDN up %u.%u.%u.%u mtu %u ch %u DNS %u.%u.%u.%u %u.%u.%u.%u: bridge mode, CommCenter's WDS call configures"
			    " pdp_ip<mux-1>\n", N2NET_IP4(n2net.ip), n2net.mtu, n2net_ch, N2NET_IP4(n2net.dns[0]), N2NET_IP4(n2net.dns[1]));
		} else {
			printf("NORD2 NET: PDN up %u.%u.%u.%u mtu %u ch %u, DNS not yet (AT+CGCONTRDP follows): bridge mode, CommCenter's WDS call"
			    " configures pdp_ip<mux-1>\n", N2NET_IP4(n2net.ip), n2net.mtu, n2net_ch);
		}
		return;
	}
	if (n2net_ifp != NULL) {
		return;
	}
	bzero(&init, sizeof(init));
	init.ver = IFNET_INIT_CURRENT_VERSION;
	init.len = sizeof(init);
	init.flags = IFNET_INIT_LEGACY | IFNET_INIT_NX_NOAUTO;
	init.uniqueid = "nord2-ccmni0";
	init.uniqueid_len = sizeof("nord2-ccmni0") - 1;
	init.name = "pdp_ip";
	init.unit = 0;
	init.family = IFNET_FAMILY_CELLULAR;
	init.type = IFT_CELLULAR;
	init.output = n2net_if_output;
	init.demux = n2net_demux;
	init.add_proto = n2net_add_proto;
	init.del_proto = n2net_del_proto;
	init.ioctl = n2net_ioctl;
	init.detach = n2net_detached;
	e_alloc = ifnet_allocate_extended(&init, &ifp);
	if (e_alloc == 0) {
		ifnet_set_mtu(ifp, n2net.mtu);
		ifnet_set_hdrlen(ifp, 0);
		ifnet_set_addrlen(ifp, 0);
		ifnet_set_baudrate(ifp, 150000000);
		ifnet_set_flags(ifp, IFF_POINTOPOINT | IFF_NOARP, IFF_POINTOPOINT | IFF_NOARP | IFF_BROADCAST | IFF_MULTICAST);
		e_att = ifnet_attach(ifp, NULL);
	}
	if (e_alloc != 0 || e_att != 0) {
		printf("NORD2 NET: pdp_ip0 not created (allocate %d attach %d)\n", e_alloc, e_att);
		if (ifp != NULL) {
			ifnet_release(ifp);
		}
		return;
	}
	n2net_ifp = ifp;
	n2net_ifps[0] = ifp;
	n2net_plumber_done = 1;
	e_reg = proto_register_plumber(PF_INET, IFNET_FAMILY_CELLULAR, n2net_plumb, n2net_unplumb);
	e_pl = proto_plumb(PF_INET, ifp);
	e_addr = n2net_addr(1, n2net.ip);
	ifnet_set_flags(ifp, IFF_UP | IFF_RUNNING, IFF_UP | IFF_RUNNING);
	if (e_addr == 0) {
		n2net_if_ip = n2net.ip;
		n2net_routes(RTM_ADD, n2net_if_ip);
	}
	n2net_publish();
	printf("NORD2 NET: %s%u up %u.%u.%u.%u mtu %u ch %u (plumber %d plumb %d addr %d) default route scoped %d global %d"
	    " (17 = EEXIST: another default kept) DNS %u.%u.%u.%u %u.%u.%u.%u (sysctl net.nord2.pdp)\n", ifnet_name(ifp), ifnet_unit(ifp),
	    N2NET_IP4(n2net.ip), n2net.mtu, n2net_ch, e_reg, e_pl, e_addr, n2net_r_scoped, n2net_r_def, N2NET_IP4(n2net.dns[0]),
	    N2NET_IP4(n2net.dns[1]));
}

/* the modem reported a new PDN address (+EIF ifup again): move pdp_ip0 to it */
static void
n2net_if_readdr(void)
{
	uint32_t old = n2net_if_ip;
	int e_del = 0, e_add;

	if (old != 0) {
		n2net_routes(RTM_DELETE, old);
		e_del = n2net_addr(0, old);
	}
	e_add = n2net_addr(1, n2net.ip);
	n2net_if_ip = e_add == 0 ? n2net.ip : 0;
	if (e_add == 0) {
		n2net_routes(RTM_ADD, n2net_if_ip);
	}
	n2net_ch = n2net.trans_id >= 0 ? (unsigned)(n2net.trans_id % 100) : n2net_ch;
	n2net_readdr++;
	n2net_publish();
	printf("NORD2 NET: pdp_ip0 address %u.%u.%u.%u -> %u.%u.%u.%u (del %d add %d) routes scoped %d global %d ch %u\n", N2NET_IP4(old),
	    N2NET_IP4(n2net.ip), e_del, e_add, n2net_r_scoped, n2net_r_def, n2net_ch);
}

/* DNS query (A www.google.com) + ICMP echo to the PDN DNS servers in turn, 8.8.8.8 last */
static void
n2net_selftest_send(void)
{
	uint32_t cand[3], dst = 0, len;
	unsigned i, k;
	int r1, r2;

	cand[0] = n2net.dns[0];
	cand[1] = n2net.dns[1];
	cand[2] = 0x08080808u;
	k = (unsigned)n2net.attempts % 3u;
	for (i = 0; i < 3u && dst == 0; i++) {
		dst = cand[(k + i) % 3u];
	}
	n2net.attempts++;
	n2net.dst = dst;
	n2net.sport = (uint16_t)(0xC000u + (unsigned)n2net.attempts);
	n2net.qid = (uint16_t)(0x4E30u + (unsigned)n2net.attempts);
	n2net.icmp_id = 0x4E32;
	n2net.sent_ms = n2net_now_ms();
	len = n2net_build_dns(n2net_stbuf, n2net.ip, dst, n2net.sport, n2net.qid, "www.google.com");
	r1 = n2d_tx(n2net_stbuf, len, n2net_ch);
	if (n2net.attempts <= 2) {
		n2net_describe("selftest", (unsigned)n2net.attempts, n2net_stbuf, len, n2net_ch);
	}
	len = n2net_build_icmp(n2net_stbuf, n2net.ip, dst, n2net.icmp_id, (uint32_t)n2net.attempts);
	r2 = n2d_tx(n2net_stbuf, len, n2net_ch);
	printf("NORD2 NET: self-test %d: DNS query + ICMP echo %u.%u.%u.%u -> %u.%u.%u.%u ch %u (tx %d %d) UL0_STA %08x\n",
	    n2net.attempts, N2NET_IP4(n2net.ip), N2NET_IP4(dst), n2net_ch, r1, r2, n2d_rd(N2D_AO_UL, 0x00));
}

static void
n2net_stats(const char *why)
{
	uint32_t ul = n2d_rd(N2D_PD_MISC, 0x00), dl = n2d_rd(N2D_PD_MISC, 0x50), busy = n2d_rd(N2D_PD_MISC, 0x60);

	if (ul) {
		n2d_wr(N2D_PD_MISC, 0x00, ul);                 /* ack the L2 status as the Linux ISR does */
	}
	if (dl) {
		n2d_wr(N2D_PD_MISC, 0x50, dl);
	}
	if (busy) {
		n2d_wr(N2D_PD_MISC, 0x60, busy);               /* drv_dpmaif_clear_ip_busy */
	}
	printf("NORD2 NET: %s t=%u tx %u pkts %u B (drop %u busy %u err %u qdrop %u) rx %u pkts %u B (drop %u msg %u dummy %u frag %u"
	    " err %u noif %u nombuf %u) hwerr %u reload %u\n", why, sweet_tick_ms, n2d.tx_pkts, n2d.tx_bytes, n2d.tx_drop, n2d.tx_busy,
	    n2d.tx_err, n2net_q_drop, n2d.rx_pkts, n2d.rx_bytes, n2d.rx_drop, n2d.rx_msg, n2d.rx_dummy, n2d.rx_frag, n2d.rx_err,
	    n2net_rx_noif, n2net_rx_nombuf, n2d.hw_err, n2d.reloads);
	printf("NORD2 NET: %s regs PIT_STA2 %08x (sw rd %u) BAT_STA2 %08x FRGBAT_STA2 %08x UL0_STA %08x L2 ul %08x dl %08x busy %08x"
	    " DL_DBG1 %08x UL_DBG2 %08x\n", why, n2d_rd(N2D_AO_DL, 0x28), n2d.pit_rd, n2d_rd(N2D_AO_DL, 0x18), n2d_rd(N2D_AO_DL, 0x48),
	    n2d_rd(N2D_AO_UL, 0x00), ul, dl, busy, n2d_rd(N2D_PD_DL, 0xB4), n2d_rd(N2D_PD_UL, 0x88));
}

/* register state next to the values Linux had on this phone (ccci_dump_ok2.txt 149.14 s; PA-dependent fields differ) */
static void
sweet_net_snapshot(const char *tag)
{
	printf("NORD2 NET: %s ao_dl PKTINFO0 %08x (linux 00010440) PKTINFO1 %08x (00000c08) PKTINFO2 %08x (7c000d00) THRES %08x"
	    " (80be203c, masked 80be207f) FRG %08x (10000f3e)\n", tag, n2d_rd(N2D_AO_DL, 0x00), n2d_rd(N2D_AO_DL, 0x04),
	    n2d_rd(N2D_AO_DL, 0x08), n2d_rd(N2D_AO_DL, 0x0C), n2d_rd(N2D_AO_DL, 0x30));
	printf("NORD2 NET: %s pd_dl BAT %08x %08x PIT %08x %08x (want %08x %08x) PIT_CON3 %08x (04000001) DL_RES %08x (ff)"
	    " UL_RES %08x (80000400)\n", tag, n2d_rd(N2D_PD_DL, 0x08), n2d_rd(N2D_PD_DL, 0x0C), n2d_rd(N2D_PD_DL, 0x28),
	    n2d_rd(N2D_PD_DL, 0x2C), (uint32_t)(n2d_pa + N2D_OFF_PIT), ((uint32_t)((n2d_pa + N2D_OFF_PIT) >> 32) << 24) | N2D_PIT_N,
	    n2d_rd(N2D_PD_DL, 0x34), n2d_rd(N2D_PD_DL, 0xA8), n2d_rd(N2D_PD_UL, 0xA8));
	printf("NORD2 NET: %s ao_ul ARB0 %08x Q0 %08x %08x Q3 %08x %08x L1TIMR0 %08x (00018600) TIMR0 ul %08x dl %08x |"
	    " remap CFG1 %08x DOM %08x (101) CACHE %08x (706) A %08x (00e300e1) 1D %08x (00ff00fd) | CG_EN %08x (7f) WDMA %08x %08x (e...)\n",
	    tag, n2d_rd(N2D_AO_UL, 0x1C), n2d_rd(N2D_AO_UL, 0x20), n2d_rd(N2D_AO_UL, 0x24), n2d_rd(N2D_AO_UL, 0x50),
	    n2d_rd(N2D_AO_UL, 0x54), n2d_rd(N2D_AO_UL, 0x8C), n2d_rd(N2D_AO_UL, 0x80), n2d_rd(N2D_AO_UL, 0x90),
	    n2d_rd(N2D_AO_MD, 0x04), n2d_rd(N2D_AO_MD, 0x20), n2d_rd(N2D_AO_MD, 0x24), n2d_rd(N2D_AO_MD, 0x30),
	    n2d_rd(N2D_AO_MD, 0x4C), n2d_rd(N2D_PD_MISC, 0x68), n2d_rd(N2D_PD_WDMA, 0x00), n2d_rd(N2D_PD_WDMA, 0x14));
	printf("NORD2 NET: %s hw PIT_STA0..3 %08x %08x %08x %08x BAT_STA0..2 %08x %08x %08x FRGBAT_STA0..2 %08x %08x %08x\n", tag,
	    n2d_rd(N2D_AO_DL, 0x20), n2d_rd(N2D_AO_DL, 0x24), n2d_rd(N2D_AO_DL, 0x28), n2d_rd(N2D_AO_DL, 0x2C),
	    n2d_rd(N2D_AO_DL, 0x10), n2d_rd(N2D_AO_DL, 0x14), n2d_rd(N2D_AO_DL, 0x18), n2d_rd(N2D_AO_DL, 0x40),
	    n2d_rd(N2D_AO_DL, 0x44), n2d_rd(N2D_AO_DL, 0x48));
}

/* before the tight MD1 cycle: mappings, DPMAIF clocks, DMA region and tables (thread context, may block) */
static int
sweet_net_prepare(volatile uint32_t *infra)
{
	static volatile uint32_t *top;
	vm_offset_t va = 0;
	kern_return_t kr;
	ppnum_t pn;
	uint32_t cg2, cg3, cfg13;

	if (!(nord2_net & 1)) {
		printf("NORD2 NET: data path disabled (nord2_net=%#x)\n", nord2_net);
		return -1;
	}
	if (sweet_n2d_map() != 0 || infra == NULL) {
		printf("NORD2 NET: register mapping failed\n");
		return -1;
	}
	if (top == NULL) {
		top = (volatile uint32_t *)ml_io_map(0x10000000UL, 0x1000);
	}
	/* ccci_hif_dpmaif_set_clk(1): infra-dpmaif-clk = ifrao_dpmaif_main (CG3 bit 26, parent dpmaif_main_sel = CLK_CFG_13 pdn bit 23),
	 * infra-dpmaif-blk-clk = ifrao_dpmaif (CG2 bit 3); only gates that are closed are opened */
	cg2 = infra[0xAC / 4];
	cg3 = infra[0xC8 / 4];
	cfg13 = top != NULL ? top[0xE0 / 4] : 0xFFFFFFFFu;
	if (top != NULL && (cfg13 & (1u << 23))) {
		top[0xE8 / 4] = 1u << 23;
	}
	if (cg2 & (1u << 3)) {
		infra[0xA8 / 4] = 1u << 3;
	}
	if (cg3 & (1u << 26)) {
		infra[0xC4 / 4] = 1u << 26;
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 NET: clocks CG2_STA %08x -> %08x (dpmaif b3) CG3_STA %08x -> %08x (dpmaif_main b26) CLK_CFG_13 %08x -> %08x (pdn b23)"
	    " INFRA_DPMAIF_CTRL %08x\n", cg2, infra[0xAC / 4], cg3, infra[0xC8 / 4], cfg13, top != NULL ? top[0xE0 / 4] : 0, infra[0xC00 / 4]);
	printf("NORD2 NET: before: infra_ao_mem %08x CG_EN %08x remap CFG1 %08x A %08x DOM %08x PIT_CON1 %08x BAT_CON1 %08x UL0_CON1 %08x"
	    " ARB0 %08x PKTINFO0 %08x THRES %08x PIT_STA2 %08x\n", sweet_n2d_inframem[0], n2d_rd(N2D_PD_MISC, 0x68), n2d_rd(N2D_AO_MD, 0x04),
	    n2d_rd(N2D_AO_MD, 0x30), n2d_rd(N2D_AO_MD, 0x20), n2d_rd(N2D_PD_DL, 0x2C), n2d_rd(N2D_PD_DL, 0x0C), n2d_rd(N2D_AO_UL, 0x24),
	    n2d_rd(N2D_AO_UL, 0x1C), n2d_rd(N2D_AO_DL, 0x00), n2d_rd(N2D_AO_DL, 0x0C), n2d_rd(N2D_AO_DL, 0x28));
	kr = kmem_alloc_contig(kernel_map, &va, N2D_DMA_SIZE, PAGE_MASK, 0, 0, KMA_DATA | KMA_ZERO | KMA_PERMANENT, VM_KERN_MEMORY_IOKIT);
	pn = kr == KERN_SUCCESS ? pmap_find_phys(kernel_pmap, (addr64_t)va) : 0;
	if (kr != KERN_SUCCESS || pn == 0 || ((ptoa_64(pn) + N2D_DMA_SIZE - 1) >> 36) != 0) {
		printf("NORD2 NET: DMA region allocation failed (kr %d pn %#x)\n", kr, pn);
		return -1;
	}
	n2d_va = (uint8_t *)va;
	n2d_pa = ptoa_64(pn);
	n2d_sw_reset();
	flush_dcache64((addr64_t)n2d_pa, N2D_DMA_SIZE, TRUE);      /* no dirty line of the zeroing may land on HW data later */
	if (n2net_lck == NULL) {
		n2net_grp = lck_grp_alloc_init("nord2_net", LCK_GRP_ATTR_NULL);
		n2net_lck = lck_mtx_alloc_init(n2net_grp, LCK_ATTR_NULL);
	}
	n2net_prepared = 1;
	printf("NORD2 NET: dma %u KiB pa %#llx: PIT %u x16 BAT %u FRG %u (%u/%u B bufs) DRB 4x%u TX %u slots; start in the MD1-off window"
	    " (remap %s)\n", N2D_DMA_SIZE >> 10, (unsigned long long)n2d_pa, N2D_PIT_N, N2D_BAT_N, N2D_FRG_N, N2D_PKT_SZ, N2D_FRG_SZ,
	    N2D_DRB_N, N2D_DRB_N / 2, (nord2_net & 2) ? "SKIPPED" : "on");
	return 0;
}

/* inside the tight cycle, MD1 off: no printf, no delay */
static void
sweet_net_hw_start(void)
{
	if (!n2net_prepared) {
		return;
	}
	sweet_n2d_inframem[0] |= 1u << 10;                         /* INFRA_PROT_DPMAIF_BIT: DPMAIF bus protect off */
	__asm__ volatile ("dsb sy" ::: "memory");
	(void)n2d_hw_start(!(nord2_net & 2), !(nord2_net & 16));
}

/* after the cycle: results, register state, and a restart if MD1 power-on reset the block */
static void
sweet_net_report(void)
{
	if (!n2net_prepared) {
		return;
	}
	printf("NORD2 NET: dpmaif start (MD1 off): intr %d rx %d rx-enable %d -> %s\n", n2d.err[0], n2d.err[1], n2d.err[2],
	    n2d.started ? "STARTED" : "FAILED");
	sweet_net_snapshot("hw");
	if (n2d.started && (n2d_rd(N2D_PD_DL, 0x28) != (uint32_t)(n2d_pa + N2D_OFF_PIT) || (n2d_rd(N2D_PD_DL, 0x2C) & 0xFFFFu) != N2D_PIT_N)) {
		printf("NORD2 NET: PIT config did not survive MD1 power-on/BOOT_UP: starting the DPMAIF again\n");
		n2d_sw_reset();
		(void)n2d_hw_start(!(nord2_net & 2), !(nord2_net & 16));
		printf("NORD2 NET: restart: intr %d rx %d rx-enable %d -> %s\n", n2d.err[0], n2d.err[1], n2d.err[2],
		    n2d.started ? "STARTED" : "FAILED");
		sweet_net_snapshot("again");
	}
	n2net.last_stats = sweet_tick_ms;
}

/* every ring-service iteration of the modem thread (0.1-1 ms) */
static void
sweet_net_poll(void)
{
	if (!n2d.started) {
		return;
	}
	if (n2d.hw_err > 32) {                                     /* register timeouts must not starve the CCCI ring service */
		n2d.started = 0;
		printf("NORD2 NET: DPMAIF STOPPED after %u register timeouts\n", n2d.hw_err);
		n2net_stats("stop");
		return;
	}
	n2net_tx_drain();
	(void)n2d_rx_poll(128);
}

/* every 50 ms of the boot loop, every pass of the steady service: interface, NetAgent confirmation, self-test, stats, routes */
static void
sweet_net_tick(void)
{
	uint32_t now = sweet_tick_ms;
	static int confirm;

	if (!n2d.started) {
		return;
	}
	if (!sweet_steady && now - n2net.last_stats >= 10000u) {   /* bring-up only; then the 60 s status line */
		n2net.last_stats = now;
		n2net_stats("10s");
	}
	if (confirm && sweet_at_owner == 0 && !sweet_md_dead) {    /* NetAgent confirmIfst: AT+EIF=<id>, "ifst", "up", <addr type> */
		char cmd[64];
		snprintf(cmd, sizeof(cmd), "AT+EIF=%d, \"ifst\", \"up\", %d\r", n2net.trans_id, n2net.ip_type > 0 ? n2net.ip_type : 1);
		sweet_at_send(cmd);
		printf("NORD2 NET: AT+EIF=%d ifst up sent\n", n2net.trans_id);
		confirm = 0;
	} else if (n2pdn2.confirm && n2pdn2.trans_id >= 0 && sweet_at_owner == 0 && !sweet_md_dead) {   /* 0277x: cid 2's */
		char cmd[64];
		snprintf(cmd, sizeof(cmd), "AT+EIF=%d, \"ifst\", \"up\", %d\r", n2pdn2.trans_id, n2pdn2.ip_type > 0 ? n2pdn2.ip_type : 1);
		sweet_at_send(cmd);
		printf("NORD2 NET: AT+EIF=%d ifst up sent (cid 2)\n", n2pdn2.trans_id);
		n2pdn2.confirm = 0;
	}
	switch (n2net.state) {
	case 0:
		if (!sweet_md_ready || !n2net.have_ip) {
			break;
		}
		n2net_ch = n2net.trans_id >= 0 ? (unsigned)(n2net.trans_id % 100) : 0;
		n2net_if_up();
		confirm = n2net.trans_id >= 0;
		n2net.state = 1;
		n2net.t0 = now;
		n2net_def_next = now + 30000u;
		break;
	case 1:
		if (now - n2net.t0 < 1500u || (!n2net.have_dns && now - n2net.t0 < 8000u)) {
			break;
		}
		n2net_publish();                                      /* DNS from CGCONTRDP may have arrived after the interface */
		n2net.state = (nord2_net & 4) ? 3 : 2;
		n2net.next = now;
		break;
	case 2:
		if ((int32_t)(now - n2net.next) < 0) {
			break;
		}
		if (n2net.dns_ok || n2net.attempts >= 8) {
			printf("NORD2 NET: self-test %s after %d attempts (DNS replies %d, echo replies %d)\n",
			    n2net.dns_ok ? "PASSED" : "got no DNS reply", n2net.attempts, n2net.dns_ok, n2net.icmp_ok);
			n2net.state = 3;
			break;
		}
		n2net_selftest_send();
		n2net.next = now + 3000u;
		break;
	default:
		break;
	}
	if (n2net_ifp == NULL || n2net.state < 1) {
		return;
	}
	if (nord2_pdn_owner) {                                    /* 0277l: the bridge answers WDS with the new address */
		if (n2net.ip != n2net_seen_ip) {
			n2net_seen_ip = n2net.ip;
			n2net_pdn_events++;
			n2net_ch = n2net.trans_id >= 0 ? (unsigned)(n2net.trans_id % 100) : n2net_ch;
			n2net_publish();
			if (n2net_pdn_events <= 8) {
				printf("NORD2 NET: PDN address #%u %u.%u.%u.%u ch %u -> the bridge (WDS)\n", n2net_pdn_events, N2NET_IP4(n2net.ip),
				    n2net_ch);
			}
			sweet_bb_event(NORD2_BB_EV_PDN);
		}
		return;
	}
	{
		static uint32_t seen_ip, retry_ms;
		if (n2net.ip != seen_ip) {                            /* a new address from the modem: act at once */
			seen_ip = n2net.ip;
			retry_ms = now;
		}
		if (n2net.have_ip && n2net.ip != n2net_if_ip && (int32_t)(now - retry_ms) >= 0) {
			retry_ms = now + 30000u;                      /* a failed attempt is retried every 30 s */
			n2net_if_readdr();
			confirm = n2net.trans_id >= 0;
		}
	}
	if (n2net_if_ip != 0 && !(nord2_net & 8) && (int32_t)(now - n2net_def_next) >= 0) {
		n2net_def_next = now + 30000u;
		if (n2net_default_is_ours() < 0) {                    /* no default at all (e.g. Wi-Fi went away): cellular takes it */
			n2net_routes(RTM_ADD, n2net_if_ip);
			n2net_def_adds++;
			printf("NORD2 NET: no default route: added via pdp_ip0 (scoped %d global %d)\n", n2net_r_scoped, n2net_r_def);
		}
	}
}

/* p2: the steady loop sleeps at most this long while the data path runs (DL latency); 2 ms right after packets moved */
static __attribute__((unused)) unsigned
sweet_net_sleep_cap(unsigned ms)
{
	uint32_t seen = n2d.rx_pkts + n2d.tx_pkts + n2d.rx_dummy + n2d.rx_msg;

	if (!n2d.started || n2net_ifp == NULL) {                  /* no interface yet: the 16384-entry PIT ring can wait */
		return ms;
	}
	if (seen != n2net_pkts_seen || n2net_qlen != 0) {
		n2net_pkts_seen = seen;
		return 2;
	}
	return ms < 5 ? ms : 5;
}

/* p2: net part of the 60 s status line */
static void
sweet_net_status_fmt(char *b, unsigned n)
{
	if (!n2d.started) {
		snprintf(b, n, "off");
		return;
	}
	snprintf(b, n, "%s %u.%u.%u.%u tx %u/%u B rx %u/%u B drop %u/%u noif %u err %u/%u hw %u selftest %s%s",
	    n2net_ifp != NULL ? "pdp_ip0" : "no-if", N2NET_IP4(n2net_if_ip), n2d.tx_pkts, n2d.tx_bytes, n2d.rx_pkts, n2d.rx_bytes,
	    n2d.tx_drop + n2d.tx_busy + n2net_q_drop, n2d.rx_drop + n2net_rx_nombuf, n2net_rx_noif, n2d.tx_err, n2d.rx_err, n2d.hw_err,
	    n2net.dns_ok ? "ok" : (n2net.state >= 3 ? "FAIL" : "-"), n2net_def_adds + n2net_readdr ? " (re-routed)" : "");
}

static __attribute__((unused)) void
sweet_net_summary(void)
{
	if (!n2net_prepared) {
		return;
	}
	if (n2d.started) {
		n2net_stats("end");
	}
	printf("NORD2 NET: summary: dpmaif %s, ip %u.%u.%u.%u, %s, self-test %d attempts, DNS replies %d, echo replies %d\n",
	    n2d.started ? "started" : "NOT started", N2NET_IP4(n2net.ip), n2net_ifp != NULL ? "pdp_ip0 up" : "no interface",
	    n2net.attempts, n2net.dns_ok, n2net.icmp_ok);
}

/* 0281: call audio. bsd/dev/audio_call.c (patch 0280) installs these at boot: CCCI_PCM_RX (ch 4, ccci_aud) packets go to
 * it, its CCCI_PCM_TX (ch 5) speech messages are sent from the steady loop, and the bridge reports the call state. */
void (*nord2_call_ccci_rx_hook)(const uint8_t *, uint32_t);
unsigned (*nord2_call_ccci_take_hook)(uint8_t *, unsigned);
void (*nord2_call_state_hook)(unsigned, unsigned);

static unsigned sweet_call_tx;
static void
sweet_call_tick(void)
{
	uint8_t pkt[192];
	unsigned n, k;

	if (nord2_call_ccci_take_hook == NULL || !sweet_md_ready) {
		return;
	}
	for (k = 0; k < 4 && (n = nord2_call_ccci_take_hook(pkt, sizeof(pkt))) != 0; k++) {
		uint32_t msg = n == 16 ? sweet_rd32(pkt + 4) >> 16 : sweet_rd32(pkt + 12) >> 16;
		int rc = sweet_ring_tx(0, pkt, n);          /* ccci_aud: tx queue 0 (port_cfg.c) */
		sweet_call_tx++;
		if (sweet_call_tx <= 200 || rc != 0) {
			printf("NORD2 CALL: tx ch5 q0 msg 0x%04x len %u -> %s (rc %d, seq %u, t=%u ms, %s loop)\n", msg, n, rc == 0 ? "sent" : "FAILED",
			    rc, sweet_seq_out[5] - 1, sweet_tick_ms, sweet_steady ? "steady" : "boot");
		}
	}
}

/* port dispatch (proxy_dispatch_recv_skb) of one MD->AP packet (ccci_header + payload) */
static void
sweet_ccci_rx_pkt(unsigned q, const uint8_t *p, uint32_t plen)
{
	uint32_t d0, d1, chw, resv, ch, seq;

	sweet_cnt_rx++;
	if (plen < 16) {
		if (sweet_log_err++ < 20) {
			printf("NORD2 MODEM: rx q%u runt %u\n", q, plen);
		}
		return;
	}
	d0 = sweet_rd32(p);
	d1 = sweet_rd32(p + 4);
	chw = sweet_rd32(p + 8);
	resv = sweet_rd32(p + 12);
	ch = chw & 0xffffU;
	seq = (chw >> 16) & 0x7fffU;
	if (ch < SWEET_CH_MAX) {                            /* ccci_md_check_rx_seq_num (Linux force-asserts the MD; we only log) */
		int last = sweet_seq_in[ch];
		if ((chw >> 31) && last != 0 && ((((int)seq - last)) & 0x7fff) != 1) {
			if (sweet_log_err++ < 20) {
				printf("NORD2 MODEM: rx ch%u seq out of order %d -> %u\n", ch, last, seq);
			}
		} else {
			sweet_seq_in[ch] = (int)seq;
		}
	}
	if (ch == 4 && nord2_call_ccci_rx_hook != NULL) {      /* 0281: CCCI_PCM_RX speech messages -> audio_call.c */
		nord2_call_ccci_rx_hook(p, plen);
		return;
	}
	switch (ch) {
	case 0:                                             /* CCCI_CONTROL_RX: ccci_fsm_recv_control_packet */
		if (d1 == 0 /* MD_INIT_START_BOOT */ && resv != 0x5555FFFFU) {
			if (!sweet_md_ready) {
				sweet_md_ready = 1;         /* CCCI_EVENT_HS2 -> md_state READY (nothing is sent back) */
				printf("NORD2 MODEM: HS2/READY t=%u ms (control message 0x0,0x%x) rx %u tx %u rpc %u fs %u\n",
				    sweet_tick_ms, resv, sweet_cnt_rx, sweet_cnt_tx, sweet_cnt_rpc, sweet_cnt_fs);
				sweet_bb_event(NORD2_BB_EV_READY);      /* 0277 */
			}
		} else {
			printf("NORD2 MODEM: control message 0x%x,0x%x d0 %x (4 MD_EX, 5 DRV_VER_ERROR, 6 EX_REC_OK, 8 EX_PASS)\n", d1, resv, d0);
		}
		break;
	case 2:                                             /* CCCI_SYSTEM_RX: sys_msg_handler */
		if (sweet_log_sys < 40) {
			sweet_log_sys++;
			printf("NORD2 MODEM: system message (%x %x %x %x) t=%u\n", d0, d1, chw, resv, sweet_tick_ms);
		}
		if (!sweet_md_ready) {
			break;                              /* CCCI_SYSTEM_TX is refused before READY */
		}
		switch (d1) {
		case 0x114:                                 /* TEST_MSG_ID_MD2AP -> TEST_MSG_ID_AP2MD */
			sweet_send_msg(0, 3, 0x115, resv);
			break;
		case 0x116:                                 /* TEST_MSG_ID_L1CORE_MD2AP -> _AP2MD */
			sweet_send_msg(0, 3, 0x117, resv);
			break;
		case 0x105:                                 /* MD_GET_BATTERY_INFO: battery_get_bat_voltage() = 4000 (power/mt6885/misc.c) */
			sweet_send_msg(0, 3, 0x105, 4000);
			break;
		case 0x110:                                 /* MD_SW_MD1_TX_POWER_REQ -> MD_SW_MD1_TX_POWER, SWTP_NO_TX_POWER */
			sweet_send_msg(0, 3, 0x10E, 1);
			break;
		default:                                    /* SIM_TYPE, TX_POWER, RF_TEMPERATURE, ...: callbacks without a reply */
			break;
		}
		break;
	case 14:                                            /* CCCI_FS_RX: 0258 in-kernel ccci_fs server */
		sweet_cnt_fs++;
		(void)q;
		sweet_fs_rx(p, plen);
		break;
	case 10:                                            /* CCCI_UART2_RX (ttyC0): 0262 AT replies/URCs */
		sweet_at_rx(p + 16, plen - 16);
		break;
	case 32:                                            /* CCCI_RPC_RX */
		sweet_rpc(p, plen);
		break;
	default:                                            /* STATUS_RX 67, IPC 34, MD log, AT, ...: logged */
		if (sweet_log_rx < 60) {
			sweet_log_rx++;
			printf("NORD2 MODEM: rx q%u ch%u len %u t=%u: %08x %08x %08x | %08x %08x\n", q, ch, plen, sweet_tick_ms, d0, d1, resv,
			    plen >= 20 ? sweet_rd32(p + 16) : 0, plen >= 24 ? sweet_rd32(p + 20) : 0);
		}
		break;
	}
}

/* ccif_rx_collect on ring q: ccci_ringbuf_readable (header/footer checks), ccci_ringbuf_read, dispatch, ccci_ringbuf_move_rpointer */
static void
sweet_ring_rx(unsigned q)
{
	static uint8_t pkt[4096];                           /* >= CCCI_MTU + header */
	volatile uint32_t *c = sweet_ring[q];
	volatile uint8_t *data;
	uint32_t rd, wr, len, n;

	if (c == NULL || sweet_rxq[q] == 0) {
		return;
	}
	data = (volatile uint8_t *)(c + 6);                 /* rx data follows the control block */
	len = c[2];
	for (n = 0; n < 64; n++) {                          /* budget per call */
		uint32_t hdr[2], ftr[2], size, plen, adv, i, o, cp;
		rd = c[0];
		wr = c[1];
		__asm__ volatile ("dmb sy" ::: "memory");
		if (len != sweet_rxq[q] || rd >= len || wr >= len) {
			if (sweet_log_err++ < 20) {
				printf("NORD2 MODEM: rx q%u bad control rd=%u wr=%u len=%u\n", q, rd, wr, len);
			}
			return;
		}
		size = wr >= rd ? wr - rd : wr + len - rd;
		if (size < 8 + 8 + 16) {
			return;                             /* -CCCI_RINGBUF_EMPTY */
		}
		for (i = 0; i < 2; i++) {
			o = (rd + 4 * i) % len;
			hdr[i] = data[o] | ((uint32_t)data[(o + 1) % len] << 8) | ((uint32_t)data[(o + 2) % len] << 16) |
			    ((uint32_t)data[(o + 3) % len] << 24);
		}
		plen = hdr[1];
		adv = (8 + plen + 8 + 7) & ~7U;
		if (hdr[0] != 0xAABBAABBU || plen > len) {
			if (sweet_log_err++ < 20) {
				printf("NORD2 MODEM: rx q%u bad header %08x %08x at rd=%u wr=%u (resync)\n", q, hdr[0], hdr[1], rd, wr);
			}
			c[0] = wr;
			return;
		}
		if (adv > size) {
			return;                             /* -CCCI_RINGBUF_NOT_COMPLETE */
		}
		for (i = 0; i < 2; i++) {
			o = (rd + adv - 8 + 4 * i) % len;
			ftr[i] = data[o] | ((uint32_t)data[(o + 1) % len] << 8) | ((uint32_t)data[(o + 2) % len] << 16) |
			    ((uint32_t)data[(o + 3) % len] << 24);
		}
		if (ftr[0] != 0xCCDDEEFFU || ftr[1] != 0xCCDDEEFFU) {
			if (sweet_log_err++ < 20) {
				printf("NORD2 MODEM: rx q%u bad footer %08x %08x len %u at rd=%u (resync)\n", q, ftr[0], ftr[1], plen, rd);
			}
			c[0] = wr;
			return;
		}
		cp = plen < sizeof(pkt) ? plen : (uint32_t)sizeof(pkt);
		for (i = 0; i < cp; i++) {
			pkt[i] = data[(rd + 8 + i) % len];
		}
		__asm__ volatile ("dmb sy" ::: "memory");
		c[0] = (rd + adv) % len;                    /* rx_control.read */
		__asm__ volatile ("dsb sy" ::: "memory");
		sweet_ccci_rx_pkt(q, pkt, cp);
	}
}

/* 0258: serve the rings at 0.1-1 ms granularity for `ms` milliseconds (the modem's FS/RPC requests are synchronous: one per 50 ms poll
 * would make the ~3750 boot-time file operations take minutes).  Only ring doorbells (bits 0-14) are acknowledged here. */
static void
sweet_ccci_poll(volatile uint32_t *ccif, unsigned ms)
{
	unsigned us = 0, q;

	while (us < ms * 1000) {
		unsigned before = sweet_cnt_rx;
		uint32_t rch = ccif[0x10 / 4] & 0x7fffU;
		if (rch) {
			ccif[0x14 / 4] = rch;
		}
		for (q = 0; q < 16; q++) {
			sweet_ring_rx(q);
			if (sweet_tx_pending & (1U << q)) {
				sweet_ccif_kick(q);
			}
		}
		sweet_net_poll();                           /* N2NET: DPMAIF DL/UL */
		if (sweet_cnt_rx != before) {
			delay_for_interval(100, NSEC_PER_USEC);
			us += 100;
		} else {
			delay_for_interval(1, NSEC_PER_MSEC);
			us += 1000;
		}
	}
}


/*
 * 0215: answer the modem's first CCIF message (runtime-data handshake, AP_MD_HS_V2 with multi-MD-MPU).  The AP runtime data below is the
 * byte-exact dump of what Linux's ccci_md_prepare_runtime_data() wrote on this phone (ccci_dump, "AP runtime data", 39 features);
 * the SRAM answer is ap_query_md_feature_v2_1 as config_ap_runtime_data_v2_1() fills it.  All addresses are MD views from the LK memory
 * map (identical at every boot).
 */
extern vm_offset_t ml_io_map_wcomb(vm_offset_t phys_addr, vm_size_t size);

static const uint32_t sweet_modem_aprt[216] = {
	0x00000200, 0x00000010, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000201, 0x00000008,
	0x40110000, 0x00010000, 0x00000202, 0x00000008,
	0x40139000, 0x000b4280, 0x00000103, 0x00000000,
	0x00000104, 0x00000000, 0x00000205, 0x00000010,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000206, 0x00000010, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000207, 0x00000010,
	0x6465fc27, 0x00000000, 0x00000000, 0x00000000,
	0x00000208, 0x00000010, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000209, 0x00000010,
	0x00000000, 0x0000007d, 0x00000000, 0x00000000,
	0x0000020a, 0x00000010, 0x00000003, 0x00000000,
	0x00000000, 0x00000000, 0x0000020b, 0x00000010,
	0x4b47ceae, 0x00000000, 0x00000000, 0x00000000,
	0x0000020c, 0x00000010, 0x00000004, 0x00000000,
	0x00000000, 0x00000000, 0x0000020d, 0x00000008,
	0xe0000000, 0x0246d37c, 0x0000020e, 0x00000008,
	0x40120000, 0x00008000, 0x0000020f, 0x00000010,
	0x40128000, 0x00001000, 0x49500000, 0x00178000,
	0x00000210, 0x00000008, 0x49700000, 0x01e00000,
	0x00000211, 0x00000008, 0x40129000, 0x00002000,
	0x00000212, 0x00000008, 0x4012b000, 0x00001000,
	0x00000213, 0x00000010, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000214, 0x00000004,
	0x00000df0, 0x00000115, 0x00000000, 0x00000116,
	0x00000000, 0x00000117, 0x00000000, 0x00000118,
	0x00000000, 0x00000219, 0x00000008, 0x00000000,
	0x00000000, 0x0000021a, 0x00000008, 0x4012c000,
	0x0000d000, 0x0000021b, 0x00000000, 0x0000021c,
	0x00000008, 0x401ed400, 0x0001e400, 0x0000011d,
	0x00000000, 0x0000021e, 0x00000008, 0x48000000,
	0x00d80000, 0x0000021f, 0x00000008, 0x4b500000,
	0x00060000, 0x00000220, 0x00000004, 0x00000000,
	0x00000121, 0x00000000, 0x00000122, 0x00000000,
	0x00000223, 0x00000008, 0x40100000, 0x00010000,
	0x00000224, 0x00000008, 0x48d80000, 0x00700000,
	0x00000125, 0x00000000, 0x00000226, 0x00000030,
	0x00000000, 0x1e000000, 0xe0000000, 0x00000000,
	0x40000000, 0x01800000, 0x8e000000, 0x00000000,
	0x48000000, 0x03560000, 0x8a000000, 0x00000000,
	0x00000127, 0x00000000, 0x00000128, 0x00000000,
	0x00000029, 0x00000000, 0x0000012a, 0x00000000,
	0x0000012b, 0x00000000, 0x0000012c, 0x00000000,
	0x0000012d, 0x00000000, 0x0000002e, 0x00000000,
	0x0000002f, 0x00000000, 0x00000030, 0x00000000,
	0x00000031, 0x00000000, 0x00000032, 0x00000000,
	0x00000033, 0x00000000, 0x00000034, 0x00000000,
	0x00000035, 0x00000000, 0x00000036, 0x00000000,
	0x00000037, 0x00000000, 0x00000038, 0x00000000,
	0x00000039, 0x00000000, 0x0000003a, 0x00000000,
	0x0000003b, 0x00000000, 0x0000003c, 0x00000000,
	0x0000003d, 0x00000000, 0x0000003e, 0x00000000,
	0x0000003f, 0x00000000, 0x00000000, 0x00000000
};

static void
sweet_modem_handshake(volatile uint32_t *ccif, uint32_t rch)
{
	volatile uint32_t *rt = (volatile uint32_t *)ml_io_map(0x8e11e000UL, 0x1000);
	unsigned i;

	if (rt == NULL) {
		printf("NORD2 MODEM: handshake: runtime-data mapping failed\n");
		return;
	}
	sweet_ccci_reset();                                            /* 0254: HS1 seen, seq numbers as after ccif_start */
	ccif[0x14 / 4] = rch;                                          /* APCCIF_ACK */
	__asm__ volatile ("dsb sy" ::: "memory");
	for (i = 0; i < 216; i++) {
		rt[0x800 / 4 + i] = sweet_modem_aprt[i];                   /* RAW_RUNTIME_DATA @ 0x8e11e800 */
	}
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 MODEM: readback AP_RT: %08x %08x %08x %08x %08x %08x\n", rt[0x200], rt[0x201], rt[0x202], rt[0x203], rt[0x204], rt[0x205]);
	ccif[(0x100 + 0xF0) / 4] = 0;                                  /* up_header.data[0] (SRAM+0xF0) */
	ccif[(0x100 + 0xF4) / 4] = 0xAC;                               /* data[1] = packet size */
	ccif[(0x100 + 0xF8) / 4] = 1;                                  /* channel = CCCI_CONTROL_TX */
	ccif[(0x100 + 0xFC) / 4] = 0x5555FFFFU;                        /* MD_INIT_CHK_ID */
	for (i = 0; i < 156 / 4; i++) {
		ccif[(0x200 / 4) + i] = 0;                       /* ap_rt_data @ SRAM+0x100 */
	}
	ccif[(0x200 / 4) + 0] = 0x43434349U;                 /* head */
	ccif[(0x200 / 4) + 1] = 0x00001000U;                 /* feature_set[1]: version 1, mask 0 */
	ccif[(0x200 / 4) + 17] = 2;                          /* share_memory_support = MULTI_MD_MPU_SUPPORT */
	ccif[(0x200 / 4) + 18] = 0x4011e800U;                /* ap_runtime_data_addr */
	ccif[(0x200 / 4) + 19] = 0x800;
	ccif[(0x200 / 4) + 20] = 0x4011f000U;                /* md_runtime_data_addr */
	ccif[(0x200 / 4) + 21] = 0x800;
	ccif[(0x200 / 4) + 22] = 0x40000000U;                /* noncached_mpu_start/size */
	ccif[(0x200 / 4) + 23] = 0x1800000U;
	ccif[(0x200 / 4) + 24] = 0x48000000U;                /* cached_mpu_start/size */
	ccif[(0x200 / 4) + 25] = 0x3560000U;
	ccif[(0x200 / 4) + 38] = 0x43434349U;                /* tail (offset 0x98) */
	__asm__ volatile ("dsb sy" ::: "memory");
	{
		volatile uint32_t *dbm = (volatile uint32_t *)ml_io_map(0x8e11f000UL, 0x1000);
		if (dbm != NULL) {                                         /* 0251: DBM guard (modem_sys1.c:723-748) at 0x8e11fe00 */
			for (i = 0; i < 0x200 / 4; i++) {
				dbm[0xe00 / 4 + i] = 0;
			}
			dbm[0xe00 / 4 + 0] = 0x44444444U; dbm[0xe00 / 4 + 1] = 0x44444444U;
			dbm[0xe00 / 4 + 42] = 0x44444444U; dbm[0xe00 / 4 + 43] = 0x44444444U;
			__asm__ volatile ("dsb sy" ::: "memory");
		}
	}
	ccif[0x04 / 4] = 1U << 15;                                     /* md_ccif_send(H2D_SRAM): BUSY */
	ccif[0x0C / 4] = 15;                                           /* TCHNUM */
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 MODEM: handshake: ACK %08x done, runtime data (0x360 B) + SRAM answer sent on ch15\n", rch);
}



/*
 * 0213: MD1 MTCMOS power cycle as in Linux clk-mt6885-pg.c spm_mtcmos_ctrl_md1().  In Linux the domain is OFF when ccci starts the
 * modem (the kernel switches it off at boot) and clk_prepare_enable(scp-sys-md1-main) powers it on fresh; LK leaves it ON on iOS.
 * Every wait is bounded; on a timeout the function returns -1 and the caller stops.
 */
static int
sweet_modem_wait(volatile uint32_t *reg, uint32_t mask, uint32_t want, const char *what)
{
	unsigned n;
	for (n = 0; n < 20000; n++) {
		if ((*reg & mask) == want) {
			return 0;
		}
		delay_for_interval(10, NSEC_PER_USEC);
	}
	printf("NORD2 MODEM: md1 power: timeout waiting for %s (reg=%08x)\n", what, *reg);
	return -1;
}

static int
sweet_modem_md1_power(volatile uint32_t *spm, volatile uint32_t *ic, int on)
{
	const uint32_t m20 = (1U << 10) | (1U << 21) | (1U << 29), m21 = (1U << 6) | (1U << 7);

	spm[0] = (0xB16U << 16) | 1U;
	__asm__ volatile ("dsb sy" ::: "memory");
	if (!on) {
		ic[0x2A0 / 4] = 1U << 7;                                   /* PROTECTEN_SET step1 */
		if (sweet_modem_wait(&ic[0x228 / 4], 1U << 7, 1U << 7, "step1 ack")) { return -1; }
		ic[0xB84 / 4] = m20;                                       /* INFRA_VDNR_SET step2_0 */
		if (sweet_modem_wait(&ic[0xB90 / 4], m20, m20, "step2_0 ack")) { return -1; }
		ic[0xBB8 / 4] = m21;                                       /* SUB_INFRA_VDNR_SET step2_1 */
		if (sweet_modem_wait(&ic[0xBC4 / 4], m21, m21, "step2_1 ack")) { return -1; }
		spm[0x300 / 4] &= ~0x4U;                                   /* PWR_ON = 0 */
		if (sweet_modem_wait(&spm[0x16C / 4], 1U, 0, "PWR_STATUS off")) { return -1; }
		spm[0x398 / 4] |= 0x3U;                                    /* MD_EXT_BUCK_ISO */
		spm[0x300 / 4] &= ~0x1U;                                   /* PWR_RST_B = 0 */
		__asm__ volatile ("dsb sy" ::: "memory");
		return 0;
	}
	spm[0x398 / 4] &= ~0x3U;
	spm[0x300 / 4] |= 0x1U;                                            /* PWR_RST_B = 1 */
	spm[0x300 / 4] |= 0x4U;                                            /* PWR_ON = 1 */
	if (sweet_modem_wait(&spm[0x16C / 4], 1U, 1U, "PWR_STATUS on")) { return -1; }
	ic[0xB88 / 4] = m20;                                               /* INFRA_VDNR_CLR */
	ic[0xBBC / 4] = m21;                                               /* SUB_INFRA_VDNR_CLR */
	ic[0x2A4 / 4] = 1U << 7;                                           /* PROTECTEN_CLR */
	__asm__ volatile ("dsb sy" ::: "memory");
	return 0;
}

#if !SWEET_AUTOTEST
/* p1: sleep up to ms, or until /dev/n2at queues bytes */
static void
sweet_svc_sleep(unsigned ms)
{
	uint64_t dl;

	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &dl);
	lck_mtx_lock(&sweet_n2at_mtx);
	if (!sweet_svc_kick) {
		(void)lck_mtx_sleep_deadline(&sweet_n2at_mtx, LCK_SLEEP_DEFAULT, (event_t)&sweet_svc_kick, THREAD_UNINT, dl);
	}
	sweet_svc_kick = 0;
	lck_mtx_unlock(&sweet_n2at_mtx);
}

/*
 * p1: steady CCCI service after the boot script, forever: ring RX/dispatch (FS, RPC, sysmsg, AT), pending doorbells, /dev/n2at, the
 * 60 s status poll.  Polls every 2 ms while there is traffic, then backs off to 5, 20 and 50 ms when idle (a write to /dev/n2at wakes
 * it at once).  Per-packet logs are off; one status line per minute.
 */
static void
sweet_modem_serve(volatile uint32_t *ccif)
{
	uint64_t t0 = mach_absolute_time(), ns;
	unsigned base = sweet_tick_ms, idle = 0, q;

	sweet_steady = 1;
	sweet_at_log = 0;
	sweet_log_rx = 60;                                  /* limits reached: no more per-packet rx/sys/rpc lines */
	sweet_log_sys = 40;
	sweet_log_rpc = 60;
	sweet_st_next_ms = base;                            /* first status line right away */
	printf("NORD2 MODEM: steady service from t=%u ms (ready=%d, AT %u/%u, fs %u ops)\n", base, sweet_md_ready, sweet_at_step,
	    (unsigned)SWEET_AT_N, sweet_fs_nops);
	sweet_bb_event(NORD2_BB_EV_STEADY);                 /* 0277 */
	for (;;) {
		unsigned before = sweet_cnt_rx + sweet_cnt_tx;
		uint32_t rch = ccif[0x10 / 4];

		absolutetime_to_nanoseconds(mach_absolute_time() - t0, &ns);
		sweet_tick_ms = base + (unsigned)(ns / NSEC_PER_MSEC);
		if (rch & 0x7fffU) {
			ccif[0x14 / 4] = rch & 0x7fffU;         /* ring doorbells (bits 0-14) */
		}
		if (rch & ((1U << 20) | (1U << 21))) {
			ccif[0x14 / 4] = rch & ((1U << 20) | (1U << 21));
		}
		if ((rch & 0x10000U) && !sweet_md_dead) {
			sweet_md_dead = 1;
			printf("NORD2 MODEM: EXCEPTION signalled by the MD (rch=%08x) at t=%u s\n", rch, sweet_tick_ms / 1000);
			sweet_bb_event(NORD2_BB_EV_DEAD);       /* 0277 */
			sweet_ee_peek();
		}
		for (q = 0; q < 16; q++) {
			sweet_ring_rx(q);
			if (sweet_tx_pending & (1U << q)) {
				sweet_ccif_kick(q);
			}
		}
		sweet_net_poll();                                   /* N2NET: DPMAIF DL/UL */
		sweet_at_tick();
		sweet_call_tick();                                  /* 0281: call-audio speech messages */
		sweet_net_tick();                                   /* N2NET: pdp_ip0 address/routes, self-test */
		if (sweet_cnt_rx + sweet_cnt_tx != before || sweet_tx_pending) {
			idle = 0;
			sweet_svc_sleep(2);
		} else {
			idle++;
			sweet_svc_sleep(sweet_net_sleep_cap(idle < 25 ? 5 : (idle < 100 ? 20 : 50)));   /* N2NET: <= 5 ms with data */
		}
	}
}

/* p1: the modem's own kernel thread (a thread call must not be held forever) */
static void
sweet_modem_thread(__unused void *arg, __unused wait_result_t wr)
{
	sweet_bb_event(NORD2_BB_EV_THREAD);                 /* 0277: the bridge publishes AppleBaseband & co. (boot-arg n2bb) */
	sweet_modem_start_inner();                          /* returns only when the power-on failed */
	sweet_fs_dev_close();
	printf("NORD2 MODEM: service thread ended\n");
}
#endif /* !SWEET_AUTOTEST */

static void
sweet_modem_start(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
#if SWEET_AUTOTEST
	sweet_n2at_init();
	sweet_modem_start_inner();
	printf("NORD2 MODEM: test finished\n");
	delay_for_interval(20000, NSEC_PER_MSEC);
	sweet_reset_now("modem test finished");
#else
	thread_t th;

	sweet_n2at_init();
	if (kernel_thread_start(sweet_modem_thread, NULL, &th) == KERN_SUCCESS) {
		thread_deallocate(th);
	} else {
		printf("NORD2 MODEM: cannot start the service thread\n");
	}
#endif
}

static void
sweet_modem_start_inner(void)
{
	volatile uint32_t *infra = (volatile uint32_t *)ml_io_map(0x10001000UL, 0x1000);
	volatile uint32_t *spm = (volatile uint32_t *)ml_io_map(0x10006000UL, 0x1000);
	volatile uint32_t *ccif = (volatile uint32_t *)ml_io_map(0x10209000UL, 0x1000);
	uint64_t r[4];
	uint32_t last_rch = 0xdeadbeef, last_busy = 0xdeadbeef, last_start = 0xdeadbeef, last_sram[8] = { 0 };
	uint32_t v;
	unsigned i, k;
	int dumped = 0, answered = 0, ee_done = 0;
	unsigned rch_dumps = 0, change_logs = 0;

	if (infra == NULL || spm == NULL || ccif == NULL) {
		printf("NORD2 MODEM: start: mapping failed\n");
		return;
	}
	sweet_ccif_ap = ccif;                                    /* 0254: tx doorbell (sweet_ccif_kick) */
	printf("NORD2 MODEM: start: begin (infracfg_ao+0xF0C=%08x)\n", infra[0xF0C / 4]);
	(void)sweet_net_prepare(infra);                         /* N2NET: mappings, DPMAIF clocks, DMA rings (may block, prints); first, so
	                                                         * the c8x ring-write -> MD1-cycle timing stays as it was */
	/*
	 * ccci_md_clear_smem(first_boot) as Linux does it: only the listed regions, never the unused rest of the bank
	 * (0x8e4c0000 reads hang, 0x8e000000-0x8e0fffff is not a region).  NCLR_FIRST regions are skipped.
	 */
	{
		static const struct { uintptr_t pa; uint32_t len; const char *name; } clr[] = {
			{ 0x8e100000UL, 0x0,       "MD_WIFI_PROXY (0247: not cleared, connsys may use it)" },
			/* 0242: skipped (flaky AP hang while clearing them with the LK-started MD1 alive): { 0x8e110000UL, 0x18000,   "DBG..SCP" }, */
			/* 0242: skipped (same): { 0x8e129000UL, 0x3000,    "NETD+USB" }, */
			/* skipped in 0212 (hang while clearing, being located): { 0x8e139000UL, 0xd2800,   "CCISM_MCU+EXP" }, */
			/* skipped in 0212 (hang while clearing, being located): { 0x8ad80000UL, 0x700000,  "MD_NVRAM_CACHE" }, */
			/* skipped in 0212 (hang while clearing, being located): { 0x8b500000UL, 0x200000,  "CCB" }, */
			/* skipped in 0212 (hang while clearing, being located): { 0x8b700000UL, 0x1e00000, "RAW_DHL/MDM" }, */
		};
		for (unsigned r = 0; r < sizeof(clr) / sizeof(clr[0]); r++) {
			uint32_t off;
			printf("NORD2 MODEM: start: clear %s pa %#lx len %#x\n", clr[r].name, (unsigned long)clr[r].pa, clr[r].len);
			/* 0266: no pause before the MD1 cycle */
			for (off = 0; off < clr[r].len; off += 0x10000) {
				uint32_t n = clr[r].len - off < 0x10000 ? clr[r].len - off : 0x10000;
				volatile uint64_t *sm = (volatile uint64_t *)ml_io_map(clr[r].pa + off, (n + 0xfff) & ~0xfffU);
				uint32_t j;
				if (sm == NULL) {
					printf("NORD2 MODEM: start: map failed pa %#lx\n", (unsigned long)(clr[r].pa + off));
					return;
				}
				for (j = 0; j < n / 8; j++) {
					sm[j] = 0;
				}
				__asm__ volatile ("dsb sy" ::: "memory");
				if ((off & 0xfffff) == 0xf0000 || off + 0x10000 >= clr[r].len) {
					printf("NORD2 MODEM: start: %s cleared through %#lx\n", clr[r].name, (unsigned long)(clr[r].pa + off + n));
					/* 0266: no pause before the MD1 cycle */
				}
			}
		}
	}
	printf("NORD2 MODEM: start: smem regions cleared\n");
	sweet_quiet = 1;                                /* 0261: rings first, immediately (Device mappings), quiet */
	sweet_prep_rc = sweet_rings_init();
	{
		int ccb_rc = sweet_ccb_ctrl_init();   /* 0269: CCB buffer headers, as vendor ccci_mdinit before DO_START_MD */
		sweet_quiet = 0;
		printf("NORD2 MODEM: start: ccb ctrl rc=%d was %08x %08x %08x\n", ccb_rc, sweet_ccb_was[0], sweet_ccb_was[1], sweet_ccb_was[2]);
	}
	printf("NORD2 MODEM: start: rings written rc=%d\n", sweet_prep_rc);
	/* 0257: no CCIF hw reset */
	printf("NORD2 MODEM: step: %s\n", "ap sram reset");
	/* 0266: no pause before the MD1 cycle */
	for (i = 0; i < 0x200 / 4; i++) {
		ccif[0x100 / 4 + i] = 0;                    /* md_ccif_sram_reset */
	}
	/* 0260: MD-side CHDATA reset removed */
	ccif[0x2F4 / 4] = 0x7274626EU;                  /* "nbtr" magic + RAW_MDSS_DBG (MD view) base/size, as the golden Linux SRAM tail */
	ccif[0x2F8 / 4] = 0x40110800U;
	ccif[0x2FC / 4] = 0x3800U;
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 MODEM: ccif reset + sram tail %08x %08x %08x\n", ccif[0x2F4 / 4], ccif[0x2F8 / 4], ccif[0x2FC / 4]);
	printf("NORD2 MODEM: step: tight cycle next\n");
	/* 0266: no pause before the MD1 cycle */
	/* 0224: MD1 off -> checks -> on as one tight sequence (no printf/delay inside), after the clears and ring init, to keep the bus-protect window minimal */
	{
		uint32_t pwr_before = spm[0x16C / 4];
		uint64_t c0[4], c1[4], c2[4], f0[4];
		uint32_t v2;
		int rc_off, rc_on;

		/* 0261: prep_rc moved to sweet_prep_rc */

		rc_off = sweet_modem_md1_power(spm, infra, 0);
		if (rc_off == 0) {
			sweet_net_hw_start();                   /* N2NET: Linux dpmaif_start, MD1 off (Linux modem-reset order) */
			sweet_modem_smc(6, 3, c0);             /* CHECK_DONE */
			sweet_modem_smc(6, 2, c1);             /* CHECK_FLAG */
			sweet_modem_smc(6, 4, c2);             /* BOOT_STATUS */
			v2 = infra[0xF0C / 4];
			infra[0xF0C / 4] = (v2 & ~0xFFU) | 0x21U;
			spm[0] = 0x0B160001U;
			spm[8 / 4] |= (1U << 21);
			__asm__ volatile ("dsb sy" ::: "memory");
			sweet_modem_smc(7, 0, f0);              /* flight mode off */
			rc_on = sweet_modem_md1_power(spm, infra, 1);
			if (rc_on == 0) {
				sweet_dpmaif_init();                /* Linux: hif start (dpmaif_start) after power-on, before BOOT_UP */
				sweet_modem_smc(6, 0, r);           /* MD_KERNEL_BOOT_UP */
			}
		} else {
			rc_on = -1;
		}
		printf("NORD2 MODEM: start: tight cycle: pwr_before=%08x off=%d on=%d now=%08x CHECK_DONE=%llx FLAG=%llx %llx BOOT_STATUS=%llx %llx %llx BOOT_UP=%llx %llx %llx\n",
		    pwr_before, rc_off, rc_on, spm[0x16C / 4], c0[0], c1[0], c1[1], c2[0], c2[1], c2[2], r[0], r[1], r[2]);
		printf("NORD2 MODEM: start: rings were written before the cycle rc=%d\n", sweet_prep_rc);
		printf("NORD2 MODEM: start: dpmaif after power-on: infra_ao_mem+0 %08x -> %08x PD_MISC_CG_EN was %08x\n", sweet_dp_val[0], sweet_dp_val[1], sweet_dp_val[2]);
		sweet_net_report();                             /* N2NET */
		if (rc_off || rc_on) {
			return;
		}
	}
	goto after_power_on;
	/* md_start_platform: BROM check */
	sweet_modem_smc(6, 3 /* MD_CHECK_DONE */, r);
	printf("NORD2 MODEM: start: CHECK_DONE -> %llx\n", r[0]);
	sweet_modem_smc(6, 2 /* MD_CHECK_FLAG */, r);
	printf("NORD2 MODEM: start: CHECK_FLAG -> %llx %llx %llx %llx\n", r[0], r[1], r[2], r[3]);
	sweet_modem_smc(6, 4 /* MD_BOOT_STATUS */, r);
	printf("NORD2 MODEM: start: AP BOOT_STATUS -> %llx %llx %llx\n", r[0], r[1], r[2]);
	/* md_cd_power_on */
	v = infra[0xF0C / 4];
	infra[0xF0C / 4] = (v & ~0xFFU) | 0x21U;                 /* INFRA_AO_MD_SRCCLKENA */
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 MODEM: start: SRCCLKENA %08x -> %08x\n", v, infra[0xF0C / 4]);
	spm[0] = 0x0B160001U;                                /* spm_sleep_base */
	v = spm[8 / 4];
	spm[8 / 4] = v | (1U << 21);
	__asm__ volatile ("dsb sy" ::: "memory");
	printf("NORD2 MODEM: start: spm+8 %08x -> %08x\n", v, spm[8 / 4]);
	sweet_modem_smc(7 /* MD_FLIGHT_MODE_SET */, 0, r);
	printf("NORD2 MODEM: start: flight mode off -> %llx %llx\n", r[0], r[1]);
	if (sweet_modem_md1_power(spm, infra, 1)) {
		return;
	}
	printf("NORD2 MODEM: start: MD1 domain on, PWR_STATUS=%08x\n", spm[0x16C / 4]);
	/* md_cd_let_md_go */
	sweet_modem_smc(6, 0 /* MD_KERNEL_BOOT_UP */, r);
	printf("NORD2 MODEM: start: KERNEL_BOOT_UP -> %llx %llx %llx\n", r[0], r[1], r[2]);
after_power_on:
	;

	for (k = 0; k < 6000; k++) {                             /* 0270: 300 s at 50 ms (registration polling) */
		uint32_t rch = ccif[0x10 / 4], busy = ccif[0x04 / 4], start = ccif[0x08 / 4];
		sweet_tick_ms = k * 50;
		sweet_at_tick();
		sweet_call_tick();                                   /* 0281: speech messages also during the (up to 300 s) boot loop */
		sweet_net_tick();                                    /* N2NET: pdp_ip0, self-test, stats */
		{
			unsigned qq;
			for (qq = 0; qq < 16; qq++) {
				if (rch & (1U << qq) && qq != 15) {
					ccif[0x14 / 4] = 1U << qq;                      /* ACK this ring queue */
				}
				if (answered) {
					sweet_ring_rx(qq);                          /* 0254: dispatch + RPC/sys replies */
				}
			}
			if (rch & ((1U << 20) | (1U << 21))) {                  /* AP_MD_PEER_WAKEUP / AP_MD_SEQ_ERROR (md_cd_ccif_isr acks) */
				if (rch & (1U << 21)) {
					printf("NORD2 MODEM: MD reports AP->MD sequence error (rch=%08x) t=%u\n", rch, k * 50);
				}
				ccif[0x14 / 4] = rch & ((1U << 20) | (1U << 21));
			}
			for (qq = 0; qq < 16; qq++) {                           /* doorbells skipped while BUSY was set */
				if (sweet_tx_pending & (1U << qq)) {
					sweet_ccif_kick(qq);
				}
			}
		}
		int changed = (rch != last_rch) || (busy != last_busy) || (start != last_start);
		for (i = 0; i < 8; i++) {
			uint32_t w = ccif[0x100 / 4 + i];
			if (w != last_sram[i]) {
				changed = 1;
			}
		}
		if ((rch & 0x8000U) && !answered) {                     /* 0215: runtime-data handshake */
			answered = 1;
			sweet_modem_handshake(ccif, rch);
			delay_for_interval(300, NSEC_PER_MSEC);
			printf("NORD2 MODEM: after answer: rch=%08x busy=%08x start=%08x\n", ccif[0x10 / 4], ccif[0x04 / 4], ccif[0x08 / 4]);
		}
		if (rch != 0 && !dumped) {                              /* 0214: full SRAM dump on the first MD message */
			unsigned q;
			dumped = 1;
			printf("NORD2 MODEM: start t=%u ms FIRST MESSAGE rch=%08x con=%08x irqmask?=%08x/%08x\n", k * 50, rch,
			    ccif[0], ccif[0x20 / 4], ccif[0x24 / 4]);
			for (q = 0; q < 128; q += 8) {
				printf("NORD2 MODEM: sram[%03x]: %08x %08x %08x %08x %08x %08x %08x %08x\n", q * 4,
				    ccif[0x100 / 4 + q], ccif[0x100 / 4 + q + 1], ccif[0x100 / 4 + q + 2], ccif[0x100 / 4 + q + 3],
				    ccif[0x100 / 4 + q + 4], ccif[0x100 / 4 + q + 5], ccif[0x100 / 4 + q + 6], ccif[0x100 / 4 + q + 7]);
				delay_for_interval(20, NSEC_PER_MSEC);
			}
		}
		if (answered && (rch & 0x10000U) && !ee_done) {
			ee_done = 1;
			printf("NORD2 MODEM: EXCEPTION signalled by the MD (rch=%08x) at t=%u ms\n", rch, k * 50);
			delay_for_interval(3000, NSEC_PER_MSEC);
			sweet_ee_peek();
		}
		if (answered && rch != last_rch && k > 0 && rch_dumps < 3) {    /* 0254: bounded (RPC traffic toggles rch) */
			unsigned q;
			rch_dumps++;
			printf("NORD2 MODEM: start t=%u ms rch changed to %08x (busy=%08x)\n", k * 50, rch, busy);
			for (q = 0; q < 64; q += 8) {
				printf("NORD2 MODEM: sram[%03x]: %08x %08x %08x %08x %08x %08x %08x %08x\n", q * 4,
				    ccif[0x100 / 4 + q], ccif[0x100 / 4 + q + 1], ccif[0x100 / 4 + q + 2], ccif[0x100 / 4 + q + 3],
				    ccif[0x100 / 4 + q + 4], ccif[0x100 / 4 + q + 5], ccif[0x100 / 4 + q + 6], ccif[0x100 / 4 + q + 7]);
				delay_for_interval(10, NSEC_PER_MSEC);
			}
		}
		if ((changed && change_logs < 60) || k % 40 == 0) {     /* 0254: every 2 s, plus the first 60 changes */
			change_logs += changed ? 1 : 0;
			{
				static volatile uint32_t *mdc;
				if (mdc == NULL) {
					mdc = (volatile uint32_t *)ml_io_map(0x1020a000UL, 0x1000);
				}
				if (mdc != NULL) {
					printf("NORD2 MODEM: mdside t=%u con=%08x busy=%08x start=%08x tch=%08x rch=%08x ack=%08x irq=%08x %08x\n", k * 50,
					    mdc[0], mdc[1], mdc[2], mdc[3], mdc[4], mdc[5], mdc[8], mdc[9]);
				}
			}
			printf("NORD2 MODEM: start t=%u ms rch=%08x busy=%08x start=%08x sram:", k * 50, rch, busy, start);
			for (i = 0; i < 8; i++) {
				last_sram[i] = ccif[0x100 / 4 + i];
				printf(" %08x", last_sram[i]);
			}
			printf("\n");
			last_rch = rch; last_busy = busy; last_start = start;
		}
		if (answered) {
			sweet_ccci_poll(ccif, 50);                      /* 0258: fast ring service while the modem boots */
		} else {
			delay_for_interval(50, NSEC_PER_MSEC);
		}
#if !SWEET_AUTOTEST
		if (sweet_md_ready && sweet_at_step >= SWEET_AT_N && (int32_t)(sweet_tick_ms - sweet_at_next_ms) >= 0) {
			break;                                      /* p1: boot script done -> steady service */
		}
#endif
	}
	printf("NORD2 MODEM: ccci summary: ready=%d rx %u tx %u rpc %u fs %u pending %x\n", sweet_md_ready, sweet_cnt_rx, sweet_cnt_tx,
	    sweet_cnt_rpc, sweet_cnt_fs, sweet_tx_pending);
#if !SWEET_AUTOTEST
	printf("NORD2 MODEM: fs summary: %u ops %u err, clean %u KB dirty %u KB, %u created; AT %u/%u, %u lines\n", sweet_fs_nops, sweet_fs_nerr,
	    sweet_fs_clean_bytes >> 10, sweet_fs_dirty_bytes >> 10, sweet_fs_ndyn, sweet_at_step, (unsigned)SWEET_AT_N, sweet_at_lines);
	sweet_modem_serve(ccif);                            /* never returns */
#else
	printf("NORD2 MODEM: fs summary: %u ops %u err, clean %u KB dirty %u KB, %u created\n", sweet_fs_nops, sweet_fs_nerr,
	    sweet_fs_clean_bytes >> 10, sweet_fs_dirty_bytes >> 10, sweet_fs_ndyn);
	sweet_at_emit();
	printf("NORD2 AT: summary: %u/%u commands sent, %u lines, %u bytes received\n", sweet_at_step, (unsigned)SWEET_AT_N, sweet_at_lines,
	    sweet_at_rxbytes);
	sweet_net_summary();                                     /* N2NET */
	sweet_modem_smc(6, 4, r);
	printf("NORD2 MODEM: start: end, BOOT_STATUS -> %llx %llx %llx\n", r[0], r[1], r[2]);
	/* 0215: is the CCISM_MCU hang caused by the device (nGnRnE) mapping?  Try Normal-NC (write-combine) access, log before each step. */
	{
		volatile uint32_t *w;
		printf("NORD2 MODEM: probe CCISM_MCU 0x8e139000 via wcomb mapping\n");
		delay_for_interval(300, NSEC_PER_MSEC);
		w = (volatile uint32_t *)ml_io_map_wcomb(0x8e139000UL, 0x1000);
		printf("NORD2 MODEM: probe mapped %p, reading\n", (void *)w);
		delay_for_interval(300, NSEC_PER_MSEC);
		if (w != NULL) {
			printf("NORD2 MODEM: probe CCISM_MCU word0 = %08x\n", (unsigned)w[0]);
		}
	}
	sweet_fs_dev_close();                               /* p1: drop the kept /dev/rufsroot0 open reference */
	sweet_modem_dump("start ccif0 sram-tail", 0x1020a000UL, 8);
#endif /* SWEET_AUTOTEST */
}

#if SWEET_PERSIST_TEST
extern int sync_internal(void);         /* bsd/vfs/vfs_syscalls.c: the sleep path's sync, returns within sync_timeout_seconds (5 s) */

static void
sweet_persist_reset(__unused thread_call_param_t p0, __unused thread_call_param_t p1)
{
	/*
	 * 0277i: write the file systems back before the watchdog reset, so the host's copy of the userdata partition has iOS's newest
	 * logs (diagnostics/Persist, Special) with their catalog records: dirty file data and the HFS journal now, the metadata's home
	 * blocks while sweet_reset_now waits for the console (8 s). logd's own unwritten buffer is not reached from here.
	 */
	printf("NORD2 AUTOFB: syncing file systems before the timed reset\n");
	(void)sync_internal();
	delay_for_interval(2000, NSEC_PER_MSEC);
	(void)sync_internal();
	sweet_reset_now("persistent test window over");
}
#endif

/* Called from bsd_init once MAC policies can register, before any exec. */
void sweet_amfi_policy_register(void);

void
sweet_amfi_policy_register(void)
{
	int error = mac_policy_register(&sweet_amfi_mac_conf, &sweet_amfi_mac_handle, NULL);
	printf("SWEET AMFI: platform binary policy %s (%d)\n",
	    error ? "NOT registered" : "registered", error);
	error = mac_policy_register(&sweet_sandbox_mac_conf, &sweet_sandbox_mac_handle, NULL);
	printf("SWEET Sandbox: permissive policy %s (%d)\n",
	    error ? "NOT registered" : "registered", error);
#if SWEET_AUTOTEST || SWEET_PERSIST_TEST || SWEET_ARM_FB
	sweet_arm_fastboot();                                           /* p1: test builds only: any later reset lands in fastboot */
#endif
	/* 0277k: before any process exists, so MobileGestalt's HasBaseband (an IODeviceTree node named "baseband") is true for
	 * SpringBoard, Preferences and MobileGestaltHelper from their first question on (Nord2Baseband.cpp bb_dt_node_early) */
	sweet_bb_event(NORD2_BB_EV_EARLY);
#if SWEET_PERSIST_TEST
	{
		static thread_call_t sweet_persist_reset_call;
		uint64_t dlr;
		sweet_persist_reset_call = thread_call_allocate(sweet_persist_reset, NULL);
		if (sweet_persist_reset_call != NULL) {
			clock_interval_to_deadline(420000, NSEC_PER_MSEC, &dlr);   /* 7 min of persistent service, then back to fastboot */
			thread_call_enter_delayed(sweet_persist_reset_call, dlr);
		}
	}
#endif
	sweet_modem_probe();
	sweet_modem_power();
	sweet_modem_late_start();
	sweet_modem_start_call = thread_call_allocate(sweet_modem_start, NULL);
	if (sweet_modem_start_call != NULL) {
		uint64_t dl;
		clock_interval_to_deadline(10000, NSEC_PER_MSEC, &dl);      /* 0267: +10 s = after Wi-Fi bring-up, before the LK-modem EE; 0265: +3 s (the unanswered LK modem takes iOS down at ~48 s); 0245: start earlier (was 60 s): the AP hangs in the clear/ring stage look time-dependent */
		thread_call_enter_delayed(sweet_modem_start_call, dl);
	}
}
#include "nord2_vmcomp.h"       /* 0482: VM compressor without swap by default (STARTUP hook before vm_fault_init) */
#endif /* ARM64_BOARD_CONFIG_SWEET */
