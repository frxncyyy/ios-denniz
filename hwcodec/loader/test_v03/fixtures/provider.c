/* SPDX-License-Identifier: MIT */
/* V03 test fixture: exports an OBJECT so that its consumer's undefined .dynsym entry is STT_OBJECT.
 * The consumer is loaded alone, which exercises "unresolved data imports are an error". */
int missing_data;
