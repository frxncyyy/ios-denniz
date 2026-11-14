/* SPDX-License-Identifier: MIT */
/* V03 test fixture: plain DT_REL, ARM mode, one of every relocation shape the loader implements. */
int liba_data = 0x11223344;
int *liba_ptr = &liba_data;
int dup_fn(void) { return 1; }
int liba_bss[4];
int liba_pre_ran, liba_init_ran;
int liba_fn(int x) { return x + liba_data; }
__attribute__((weak)) int weak_fn(void) { return 3; }
static void a_pre(void) { liba_pre_ran = 1; }
static void a_init(void) { liba_init_ran = 1; }
static void a_fini(void) { }
__attribute__((section(".preinit_array"), used)) static void (*const a_pre_p)(void) = a_pre;
__attribute__((section(".init_array"), used)) static void (*const a_init_p)(void) = a_init;
__attribute__((section(".fini_array"), used)) static void (*const a_fini_p)(void) = a_fini;
