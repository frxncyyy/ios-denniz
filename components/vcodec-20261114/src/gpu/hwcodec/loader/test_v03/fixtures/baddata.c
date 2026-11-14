/* SPDX-License-Identifier: MIT */
/* V03 test fixture: an undefined OBJECT import with no data shim -> n2elf_link must fail naming it. */
extern int missing_data;
int *bad_p = &missing_data;
int bad_fn(void) { return missing_data; }
