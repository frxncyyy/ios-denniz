/* SPDX-License-Identifier: MIT */
/*
 * n2vcd_selftest.h -- vcodecd --selftest: prove the interpreter's user-mode pieces on the target before anything
 * touches the vendor set or the hardware.  Owner: package IP.
 */
#ifndef N2VCD_SELFTEST_H
#define N2VCD_SELFTEST_H

/* No hardware, no vendor files, no network.  Prints "N2VCD selftest PASS/FAIL <step>" to stdout and
 * /dev/console; returns 0 when every step passed, 1 otherwise.  Safe to call before any daemon state exists. */
int n2vcd_selftest(void);

#endif /* N2VCD_SELFTEST_H */
