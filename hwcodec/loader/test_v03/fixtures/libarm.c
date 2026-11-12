/* SPDX-License-Identifier: MIT */
/* V03 test fixture: ARM mode, built against liba, calls through the GOT/PLT (liba_fn, liba_ptr) and a host-call
 * shim (shim_fn_a). Used by t_real to prove that the loader's relocated GOT values work in the real interpreter. */
extern int liba_data;
extern int *liba_ptr;
extern int liba_fn(int);
extern int shim_fn_a(int);
extern int shim_data_x;
int libarm_data = 11;
int libarm_fn(int x) { return liba_fn(x) + *liba_ptr + shim_fn_a(shim_data_x); }
