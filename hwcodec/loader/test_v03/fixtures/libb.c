/* SPDX-License-Identifier: MIT */
/* V03 test fixture: Thumb, Android packed relocations, cross-object imports and shims, duplicate symbol. */
extern int liba_data;
extern int *liba_ptr;
extern int liba_fn(int);
extern int shim_data_x;
extern int shim_fn_a(int);
extern int weak_fn(void);
int libb_data = 5;
int libb_init_ran;
int *libb_p = &liba_data;
int (*libb_f)(int) = liba_fn;
int *libb_sp = &shim_data_x;
int (*libb_sf)(int) = shim_fn_a;
int (*libb_wf)(void) = weak_fn;
int dup_fn(void) { return 2; }
int libb_fn(int x) { return liba_fn(x) + *liba_ptr + shim_fn_a(shim_data_x); }
static void b_init(void) { libb_init_ran = 1; }
__attribute__((section(".init_array"), used)) static void (*const b_init_p)(void) = b_init;
