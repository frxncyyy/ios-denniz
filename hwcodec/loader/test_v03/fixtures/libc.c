/* SPDX-License-Identifier: MIT */
/* V03 test fixture: Android packed + RELR, dependency chain, one unresolved import. */
extern int libb_data;
extern int libb_fn(int);
extern int dup_fn(void);
extern int never_defined_fn(int);
extern int shim_data_x;
extern int weak_missing_fn(int) __attribute__((weak));
int libc_data = 7;
int libc_init_ran;
int *libc_p = &libb_data;
int (*libc_df)(void) = dup_fn;
int *libc_sd = &shim_data_x;
int (*libc_wm)(int) = weak_missing_fn;
int libc_fn(int x) { return libb_fn(x) + never_defined_fn(x) + dup_fn(); }
static void c_init(void) { libc_init_ran = 1; }
__attribute__((section(".init_array"), used)) static void (*const c_init_p)(void) = c_init;
