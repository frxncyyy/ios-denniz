// SPDX-License-Identifier: GPL-2.0
/*
 * Nord 2 module-loading smoke test: proves an out-of-tree module built
 * against the running kernel's source/config loads, and reports the
 * non-exported symbols a future XNU loader would need. Touches no hardware.
 */
#include <linux/kprobes.h>
#include <linux/module.h>
#include <asm/memory.h>

static const char *const syms[] = {
	"__cpu_soft_restart", "idmap_pg_dir", "swapper_pg_dir",
	"freeze_secondary_cpus", "disable_nonboot_cpus", "device_shutdown",
	"kernel_restart_prepare", "machine_shutdown", "__flush_dcache_area",
	"cpu_do_switch_mm", "idmap_t0sz", "smp_send_stop", "migrate_to_reboot_cpu",
	"__hyp_stub_vectors", "is_hyp_mode_available", "__boot_cpu_mode",
};

typedef unsigned long (*lookup_fn)(const char *name);

/* kallsyms_lookup_name is not exported here; a kprobe resolves it. */
static lookup_fn find_lookup(void)
{
	struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };
	lookup_fn fn;

	if (register_kprobe(&kp) < 0)
		return NULL;
	fn = (lookup_fn)kp.addr;
	unregister_kprobe(&kp);
	return fn;
}

static int __init nord2_hello_init(void)
{
	lookup_fn kallsyms_lookup_name = find_lookup();
	int i;

	if (!kallsyms_lookup_name) {
		pr_err("nord2_hello: kprobe on kallsyms_lookup_name failed\n");
		return -ENOENT;
	}

	pr_info("nord2_hello: loaded, PAGE_OFFSET=%lx kimage_voffset=%llx\n",
		(unsigned long)PAGE_OFFSET, (unsigned long long)kimage_voffset);
	for (i = 0; i < ARRAY_SIZE(syms); i++) {
		unsigned long a = kallsyms_lookup_name(syms[i]);

		pr_info("nord2_hello: %-24s %s\n", syms[i], a ? "found" : "MISSING");
	}
	return 0;
}

static void __exit nord2_hello_exit(void)
{
	pr_info("nord2_hello: unloaded\n");
}

module_init(nord2_hello_init);
module_exit(nord2_hello_exit);
MODULE_LICENSE("GPL");
