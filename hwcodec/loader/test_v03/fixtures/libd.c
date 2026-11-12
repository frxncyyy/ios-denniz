/* SPDX-License-Identifier: MIT */
/* V03 test fixture: DT_RELR only, no imports. */
int libd_data = 9;
int libd_init_ran;
int *libd_p = &libd_data;
int libd_fn(int x) { return x + libd_data; }
static void d_init(void) { libd_init_ran = 1; }
__attribute__((section(".init_array"), used)) static void (*const d_init_p)(void) = d_init;
