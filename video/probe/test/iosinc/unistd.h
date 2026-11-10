/* test scaffolding, not a product header: minimal declarations so that probe/*.c can be compiled for
 * arm64-apple-ios16.0 without an SDK.  The real build uses sdk/sysinc (WP01).  SPDX-License-Identifier: MIT */
#ifndef N2_MIN_UNISTD_H
#define N2_MIN_UNISTD_H
#include <sys/types.h>
ssize_t read(int, void *, size_t);
ssize_t write(int, const void *, size_t);
int close(int);
int fsync(int);
unsigned sleep(unsigned);
int usleep(unsigned);
int access(const char *, int);
void _exit(int) __attribute__((noreturn));
pid_t getpid(void);
int unlink(const char *);
int pipe(int [2]);
pid_t fork(void);
#define F_OK 0
#endif
