/* SPDX-License-Identifier: MIT
 * n2sys.h -- the few Darwin (iOS 16 arm64) libSystem declarations n2mnld needs. The build is freestanding (-nostdinc, like
 * the other gpu/hwcodec userland tools), so struct layouts and constants are written here from the XNU/Libc headers.
 * Everything named l_* / LX_* is the LINUX (bionic arm64) side, everything else is Darwin.
 */
#ifndef N2SYS_H
#define N2SYS_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

typedef long ssize_t; typedef long off_t; typedef long time_t; typedef unsigned int useconds_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct timeval { time_t tv_sec; int tv_usec; };            /* Darwin: suseconds_t is int, 16 bytes with padding */
typedef struct _opaque_FILE FILE; typedef struct _opaque_DIR DIR;
typedef void *pthread_t;
typedef struct { long sig; char opaque[56]; } pthread_mutex_t;
typedef struct { long sig; char opaque[40]; } pthread_cond_t;
typedef struct { long sig; char opaque[56]; } pthread_attr_t;
typedef struct { long sig; char opaque[8]; } pthread_mutexattr_t;
struct kevent { uintptr_t ident; int16_t filter; uint16_t flags; uint32_t fflags; intptr_t data; void *udata; };
struct d_sockaddr_in { uint8_t len, family; uint16_t port; uint32_t addr; char zero[8]; };
struct d_sockaddr_un { uint8_t len, family; char path[104]; };

/* Darwin constants */
#define D_O_RDONLY 0
#define D_O_WRONLY 1
#define D_O_RDWR 2
#define D_O_NONBLOCK 0x4
#define D_O_APPEND 0x8
#define D_O_CREAT 0x200
#define D_O_TRUNC 0x400
#define D_O_EXCL 0x800
#define D_O_NOCTTY 0x20000
#define D_O_DIRECTORY 0x100000
#define D_O_NOFOLLOW 0x100
#define D_O_CLOEXEC 0x1000000
#define D_EAGAIN 35
#define D_EINTR 4
#define D_EVFILT_READ (-1)
#define D_EVFILT_WRITE (-2)
#define D_EV_ADD 0x1
#define D_EV_DELETE 0x2
#define D_EV_ENABLE 0x4
#define D_EV_ONESHOT 0x10
#define D_EV_CLEAR 0x20
#define D_EV_ERROR 0x4000
#define D_EV_EOF 0x8000
#define D_PTHREAD_MUTEX_RECURSIVE 2
#define D_PTHREAD_CREATE_DETACHED 2
#define D_SOL_SOCKET 0xffff
#define D_SIGPIPE 13
#define D_SIG_IGN ((void (*)(int))1)
#define D_FIONBIO 0x8004667eul
#define D_FIONREAD 0x4004667ful
#define D_CLOCK_REALTIME 0
#define D_CLOCK_MONOTONIC_RAW 4
#define D_CLOCK_MONOTONIC 6
#define D_CLOCK_PROCESS_CPUTIME_ID 12
#define D_CLOCK_THREAD_CPUTIME_ID 16

/* libSystem */
extern int *__error(void);
extern void *malloc(size_t); extern void *calloc(size_t, size_t); extern void free(void *);
extern void *memcpy(void *, const void *, size_t); extern void *memset(void *, int, size_t); extern void *memmove(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern size_t strlen(const char *); extern int strcmp(const char *, const char *); extern int strncmp(const char *, const char *, size_t);
extern char *strcpy(char *, const char *); extern char *strncpy(char *, const char *, size_t); extern char *strcat(char *, const char *);
extern char *strncat(char *, const char *, size_t); extern char *strchr(const char *, int); extern char *strrchr(const char *, int);
extern char *strstr(const char *, const char *); extern char *strerror(int);
extern int snprintf(char *, size_t, const char *, ...);
extern int vsnprintf(char *, size_t, const char *, va_list);
extern int vsscanf(const char *, const char *, va_list); extern int vfscanf(FILE *, const char *, va_list);
extern int open(const char *, int, ...); extern int close(int); extern ssize_t read(int, void *, size_t); extern ssize_t write(int, const void *, size_t);
extern int fcntl(int, int, ...); extern int ioctl(int, unsigned long, ...); extern int pipe(int[2]);
extern int access(const char *, int); extern int chmod(const char *, unsigned short); extern int mkdir(const char *, unsigned short);
extern int chdir(const char *); extern int remove(const char *); extern int rename(const char *, const char *);
extern int stat(const char *, void *); extern int mkfifo(const char *, unsigned short); extern int fsync(int);
extern FILE *fopen(const char *, const char *); extern size_t fwrite(const void *, size_t, size_t, FILE *); extern int fflush(FILE *);
extern DIR *opendir(const char *); extern void *readdir(DIR *); extern int closedir(DIR *);
extern int usleep(useconds_t); extern unsigned int sleep(unsigned int); extern void exit(int) __attribute__((noreturn));
extern void abort(void) __attribute__((noreturn));
extern int clock_gettime(int, struct timespec *); extern int gettimeofday(struct timeval *, void *);
extern int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *); extern int pthread_mutex_destroy(pthread_mutex_t *);
extern int pthread_mutex_lock(pthread_mutex_t *); extern int pthread_mutex_unlock(pthread_mutex_t *);
extern int pthread_mutexattr_init(pthread_mutexattr_t *); extern int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
extern int pthread_cond_init(pthread_cond_t *, const void *); extern int pthread_cond_destroy(pthread_cond_t *);
extern int pthread_cond_signal(pthread_cond_t *); extern int pthread_cond_broadcast(pthread_cond_t *);
extern int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
extern int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
extern int pthread_cond_timedwait_relative_np(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
extern int pthread_attr_init(pthread_attr_t *); extern int pthread_attr_setstacksize(pthread_attr_t *, size_t);
extern int pthread_attr_setdetachstate(pthread_attr_t *, int); extern int pthread_attr_destroy(pthread_attr_t *);
extern int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
extern int pthread_key_create(unsigned long *, void (*)(void *)); extern void *pthread_getspecific(unsigned long);
extern int pthread_setspecific(unsigned long, const void *);
extern int kqueue(void);
extern int kevent(int, const struct kevent *, int, struct kevent *, int, const struct timespec *);
extern int socket(int, int, int); extern int bind(int, const void *, unsigned int); extern int listen(int, int);
extern int accept(int, void *, unsigned int *); extern int setsockopt(int, int, int, const void *, unsigned int);
extern void (*signal(int, void (*)(int)))(int);
extern double sin(double); extern double cos(double); extern int atoi(const char *);

/* n2shim.c <-> n2mnld.c */
struct n2_dev_ops {                 /* the L1 data link as libmnl sees it (read/write/ioctl on cfg2's fd) */
	ssize_t (*read)(void *buf, size_t n, int nonblock);  /* returns bytes, or -LINUX_ERRNO */
	ssize_t (*write)(const void *buf, size_t n);         /* idem */
	int (*ioctl)(unsigned long req, void *arg);           /* returns >= 0 or -LINUX_ERRNO */
};
extern const struct n2_dev_ops *n2_dev;
extern int n2_l1_fd, n2_l5_fd;      /* fds handed to libmnl in cfg2 (real descriptors, so close/fcntl stay sane) */
extern const char *n2_dir;          /* /private/var/db/n2gnss */
void n2_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* "N2MNLD ..." console + log file */
void n2_logq(const char *fmt, ...) __attribute__((format(printf, 1, 2)));  /* log file only */
void *n2_shim_lookup(const char *name);
int n2_d2l_errno(int d);
void n2_set_lerrno(int linux_errno);
void n2_shim_init(void);
/* n2trace.c */
void *n2_trace_wrap(const char *name, void *real);
void n2_trace_start(int seconds);
void n2_trace_summary(int force);
void n2_fault_install(void);
void n2_log_raw(const char *s, size_t n);
#endif
