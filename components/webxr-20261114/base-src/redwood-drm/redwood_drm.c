/* Derived from y2sios tools/mesa/redwood-drm/redwood_drm.c (MIT): the kernel calls are np_call() of the Nord2GPU
 * transport (nord2_pan.c, included below); the IOKit connection code and the WAIT_BO fast path are gone. */
/*
 * The libdrm of xf86drm.h (see there): Linux's DRM requests to the kernel's GPU driver as calls
 * of its IOKit user client. A request's number (the low byte of the ioctl code) is the call's
 * selector, its structure goes in and comes back as it is; a buffer's memory is the client
 * memory of the type that is the buffer's handle.
 *
 * freedreno knows its device by a file descriptor. It gets one that means nothing (/dev/null):
 * there is one GPU and one connection to it in a process.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <mach/mach_time.h>

#include <pthread.h>
#include <fcntl.h>
#include <sys/ioccom.h>
#include <IOKit/IOKitLib.h>
#include "nord2_pan.h"
#include "nord2_pan.c"
#include "nord2_pan_ios.c"

#include "xf86drm.h"

int64_t os_time_get_nano(void);       /* Mesa's util/os_time.c: the clock its deadlines are on */

#define REDWOOD_GPU_SERVICE     "RedwoodGPU"    /* the Adreno: Linux's msm */
#define REDWOOD_MALI_SERVICE    "RedwoodMali"   /* the Mali-G77 (Exynos 990): Linux's panfrost (iokit/Kernel/RedwoodMali.cpp) */
#define REDWOOD_SYNCOBJ_SET     0x7b            /* RedwoodMali: a syncobj's fence set, and read */
#define REDWOOD_SYNCOBJ_GET     0x7c
#define REDWOOD_GPU_SURFACE     0x7e            /* the request that is not Linux's */
#define REDWOOD_GPU_DESCRIBE    0xffffffffu     /* ... asked only what the buffer would be like */

static int connection_fd = -1;
static int panfrost;

int
drmOpenWithType(const char *name, const char *busid, int type)
{
	(void)busid;
	(void)type;
	int mali = strcmp(name, "panfrost") == 0;

	if (strcmp(name, "msm") != 0 && !mali) {
		return -1;
	}
	if (connection_fd >= 0) {
		return panfrost == mali ? dup(connection_fd) : -1;
	}
	if (!mali || np_open_iokit() != 0) {
		return -1;
	}
	connection_fd = open("/dev/null", O_RDWR);
	panfrost = 1;
	return dup(connection_fd);
}

static int
call(unsigned number, void *data, size_t size)
{
	return np_call(number, data, size, NULL) == 0 ? 0 : errno;
}

int
drmIoctl(int fd, unsigned long request, void *arg)
{
	unsigned number = (unsigned)(request & 0xff);
	size_t size = IOCPARM_LEN(request);
	int error, result;

	(void)fd;
	error = call(number, arg, size);
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
drmCommandWrite(int fd, unsigned long index, const void *data, unsigned long size)
{
	(void)fd;
	return -call((unsigned)(DRM_COMMAND_BASE + index), (void *)(uintptr_t)data, size);
}

int
drmCommandWriteRead(int fd, unsigned long index, void *data, unsigned long size)
{
	(void)fd;
	return -call((unsigned)(DRM_COMMAND_BASE + index), data, size);
}

/*
 * The driver calls itself what Linux's is called, and is of the version whose interface it has:
 * msm 1.7, buffers at addresses the kernel chooses and tells ("softpin"), nothing newer; panfrost
 * 1.3 (heap and no-exec buffers, the timestamp parameters), which is what Mesa 25.0 asks for.
 */
drmVersionPtr
drmGetVersion(int fd)
{
	drmVersionPtr version = calloc(1, sizeof(*version));

	(void)fd;
	if (version != NULL) {
		version->version_major = 1;
		version->version_minor = panfrost ? 3 : 7;
		version->name = strdup(panfrost ? "panfrost" : "msm");
		version->name_len = (int)strlen(version->name);
		version->date = strdup("");
		version->desc = strdup(panfrost ? "Mali-G77 on Darwin" : "Adreno on Darwin");
		version->desc_len = (int)strlen(version->desc);
	}
	return version;
}

void
drmFreeVersion(drmVersionPtr version)
{
	if (version != NULL) {
		free(version->name);
		free(version->date);
		free(version->desc);
		free(version);
	}
}

int
drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
	(void)fd;
	(void)capability;
	*value = 0;
	return 0;
}

void *
redwood_drm_map(int fd, uint32_t handle, uint64_t size)
{
	uint64_t address = 0, mapped = 0;

	(void)fd;
	(void)size;
	return np_map(handle, &address, &mapped) == 0 ? (void *)(uintptr_t)address : (void *)-1;
}

void
redwood_drm_unmap(int fd, uint32_t handle, void *address)
{
	(void)fd;
	(void)np_unmap(handle, (uint64_t)(uintptr_t)address);
}

int
redwood_drm_surface(int fd, struct redwood_drm_surface *surface)
{
	int error;

	(void)fd;
	surface->handle = REDWOOD_GPU_DESCRIBE;
	error = call(REDWOOD_GPU_SURFACE, surface, sizeof(*surface));
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
redwood_drm_counts(uint64_t counts[4])
{
	int error = panfrost ? call(0x7d, counts, 4 * sizeof(uint64_t)) : ENOSYS;

	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

/*
 * A buffer object handed to another part of this same process (a texture laid over a buffer's memory,
 * air_metal.c's rwa_texture_from_buffer): the "file descriptor" is the handle itself with a tag no
 * IOSurface ID has, and importing it gives the same handle back -- Panfrost then finds its own buffer object
 * and takes a reference (panfrost_bo_import). Nothing leaves the process this way.
 */
#define REDWOOD_PRIME_LOCAL     0x40000000u

int
drmPrimeHandleToFD(int fd, uint32_t handle, uint32_t flags, int *prime_fd)
{
	(void)fd;
	(void)flags;
	if (handle == 0 || (handle & REDWOOD_PRIME_LOCAL) != 0) {
		errno = EINVAL;
		return -1;
	}
	*prime_fd = (int)(handle | REDWOOD_PRIME_LOCAL);
	return 0;
}

int
drmPrimeFDToHandle(int fd, int surface, uint32_t *handle)
{
	struct redwood_drm_surface request = { .surface = (uint32_t)surface };
	int error;

	if (((uint32_t)surface & REDWOOD_PRIME_LOCAL) != 0) {
		*handle = (uint32_t)surface & ~REDWOOD_PRIME_LOCAL;
		return 0;
	}
	np_set_import_fd(fd);           /* thumbrc: the import is this descriptor's (one per Mesa screen) */
	error = call(REDWOOD_GPU_SURFACE, &request, sizeof(request));
	np_set_import_fd(-1);
	if (error != 0) {
		errno = error;
		return -1;
	}
	*handle = request.handle;
	return 0;
}

static int
none(void)
{
	errno = ENOSYS;
	return -1;
}

/* Buffers and fences as file descriptors for other programs and drivers: none of that here (drmPrimeHandleToFD
 * above names a buffer only inside this process). */
#define NONE(name, ...) int name(__VA_ARGS__) { return none(); }

/*
 * Syncobjs: the kernel's (RedwoodMali; RedwoodGPU has none). A sync file -- a fence as a file
 * descriptor, for other drivers and programs -- is a descriptor of /dev/null here, with the fence
 * it stands for kept beside it in this process: nothing outside it is handed one.
 */
#define SYNC_FILES 4096
static uint64_t sync_file_fence[SYNC_FILES];

struct syncobj_fence {
	uint32_t handle, pad;
	uint64_t fence;
};

static int
fail(int error)
{
	if (error != 0) {
		errno = error;
		return -1;
	}
	return 0;
}

int
drmSyncobjCreate(int fd, uint32_t flags, uint32_t *handle)
{
	struct drm_syncobj_create c = { .flags = flags };
	int error = call(0xbf, &c, sizeof(c));

	(void)fd;
	if (error == 0) {
		*handle = c.handle;
	}
	return fail(error);
}

int
drmSyncobjDestroy(int fd, uint32_t handle)
{
	struct drm_syncobj_destroy d = { .handle = handle };

	(void)fd;
	return fail(call(0xc0, &d, sizeof(d)));
}

int
drmSyncobjReset(int fd, const uint32_t *handles, uint32_t count)
{
	struct drm_syncobj_array a = { .handles = (uint64_t)(uintptr_t)handles, .count_handles = count };

	(void)fd;
	return fail(call(0xc4, &a, sizeof(a)));
}

int
drmSyncobjSignal(int fd, const uint32_t *handles, uint32_t count)
{
	struct drm_syncobj_array a = { .handles = (uint64_t)(uintptr_t)handles, .count_handles = count };

	(void)fd;
	return fail(call(0xc5, &a, sizeof(a)));
}

int
drmSyncobjWait(int fd, uint32_t *handles, unsigned num_handles, int64_t timeout_nsec, unsigned flags,
    uint32_t *first_signaled)
{
	/*
	 * The deadline is Mesa's (os_time_get_nano: timespec_get TIME_MONOTONIC) and the kernel reads
	 * it against mach_continuous_time. On the phone the two were 22 s apart, so every wait with a
	 * deadline had already expired when it was made: CoreAnimation on Metal saw 986 "the GPU did not
	 * finish" in two minutes, the kernel no failed job at all (24 October). What is left of the wait
	 * is carried over to the kernel's clock instead.
	 */
	if (timeout_nsec > 0 && timeout_nsec != INT64_MAX) {
		static mach_timebase_info_data_t base;
		int64_t left = timeout_nsec - os_time_get_nano();
		uint64_t now;

		if (base.denom == 0) {
			mach_timebase_info(&base);
		}
		now = mach_continuous_time() * base.numer / base.denom;
		timeout_nsec = left <= 0 ? 1 : (int64_t)now + left;
	}
	struct drm_syncobj_wait w = {
		.handles = (uint64_t)(uintptr_t)handles, .timeout_nsec = timeout_nsec, .count_handles = num_handles, .flags = flags,
	};
	int error = call(0xc3, &w, sizeof(w));

	(void)fd;
	if (error == 0 && first_signaled != NULL) {
		*first_signaled = w.first_signaled;
	}
	return fail(error);                     /* (a timeout: ETIMEDOUT, as call() has it) */
}

int
drmSyncobjExportSyncFile(int fd, uint32_t handle, int *sync_file_fd)
{
	struct syncobj_fence f = { .handle = handle };
	int error = call(REDWOOD_SYNCOBJ_GET, &f, sizeof(f)), file;

	(void)fd;
	if (error != 0) {
		return fail(error);
	}
	file = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (file < 0) {
		return -1;
	}
	if (file < SYNC_FILES) {
		sync_file_fence[file] = f.fence;
	}
	*sync_file_fd = file;
	return 0;
}

int
drmSyncobjImportSyncFile(int fd, uint32_t handle, int sync_file_fd)
{
	struct syncobj_fence f = {
		.handle = handle, .fence = sync_file_fd >= 0 && sync_file_fd < SYNC_FILES ? sync_file_fence[sync_file_fd] : 0,
	};

	(void)fd;
	return fail(call(REDWOOD_SYNCOBJ_SET, &f, sizeof(f)));
}

int
drmSyncobjHandleToFD(int fd, uint32_t handle, int *obj_fd)
{
	return drmSyncobjExportSyncFile(fd, handle, obj_fd);
}

int
drmSyncobjFDToHandle(int fd, int obj_fd, uint32_t *handle)
{
	if (drmSyncobjCreate(fd, 0, handle) != 0) {
		return -1;
	}
	return drmSyncobjImportSyncFile(fd, *handle, obj_fd);
}

int
drmCloseBufferHandle(int fd, uint32_t handle)
{
	struct drm_gem_close c = { .handle = handle };

	(void)fd;
	return fail(call(DRM_IOCTL_GEM_CLOSE & 0xff, &c, sizeof(c)));
}

/* (timeline syncobjs are for the GPUs Mesa drives through panthor, newer than this one) */
NONE(drmSyncobjTimelineWait, __attribute__((unused)) int fd, __attribute__((unused)) uint32_t *handles,
    __attribute__((unused)) uint64_t *points, __attribute__((unused)) unsigned num_handles,
    __attribute__((unused)) int64_t timeout_nsec, __attribute__((unused)) unsigned flags,
    __attribute__((unused)) uint32_t *first_signaled)
NONE(drmSyncobjTransfer, __attribute__((unused)) int fd, __attribute__((unused)) uint32_t dst_handle,
    __attribute__((unused)) uint64_t dst_point, __attribute__((unused)) uint32_t src_handle,
    __attribute__((unused)) uint64_t src_point, __attribute__((unused)) uint32_t flags)
