/*
 * An IOSurface of CoreAnimation's, as a texture of this driver.
 *
 * WHY THIS EXISTS, in one measurement. Showing a frame on this port costs 15.46 ms
 * (docs/metal.md 10.43): -getBytes: pulls a finished attachment into the process (11.96 ms,
 * 867 MB/s) and kRedwoodShow pushes it into the kernel (3.50 ms, 2962 MB/s). That is a
 * ceiling of 64.7 frames a second with nothing drawn, and about twelve of those
 * milliseconds exist for one reason only: there is nowhere else to put the picture. A
 * drawable is that somewhere else -- memory the display controller already scans out -- and
 * both copies go away, not get faster.
 *
 * WHAT COREANIMATION ACTUALLY ASKS FOR, read out of this phone's own cache (every address
 * below is in docs/metal.md 10.46 with what it was read from). -[CAMetalLayer nextDrawable]
 * (0x18829de60) ends in get_unused_drawable (0x18837cff8), which makes the surface itself
 * with CA::Render::create_iosurface_with_pixel_format (0x18833cfa8) and then calls
 * allocate_drawable_texture (0x1884b3470). That function sends a device exactly one message:
 *
 *     -newTextureWithDescriptor:iosurface:plane:   (0x1885c3060)
 *
 * with a plain MTLTextureDescriptor it has just built, the surface, and plane 0. Everything
 * else on that path is Apple's or CoreAnimation's: the descriptor's private setters
 * (-setRotation:, -setIsDrawable:, -setFramebufferOnly:, -setUsage:, -setResourceIndex:) are
 * all MTLTextureDescriptorInternal's own, and presentation never comes near a device at all
 * -- register_drawable (0x18838692c) hands the surface to the layer's CAImageQueue with
 * CAImageQueueRegisterIOSurfaceBuffer and -[CAMetalDrawable present] inserts the buffer id
 * into that queue. So the whole of what this port owes a drawable is the one method.
 *
 * AND THAT METHOD HAS FOUR OTHER CALLERS, which the first boot of this file found out the
 * hard way. A scan of QuartzCore's __text for BL to the selector stub finds five sites; the
 * four that are not allocate_drawable_texture are the window server's Metal compositor:
 * CA::OGL::MetalContext::set_destination_iosurface (0x188302550), ::update_surface
 * (0x1883163e0), ::get_surface (0x18833e724) and ::create_surface_from_iosurface
 * (0x18835e55c). They composite whatever surface a client gave the window server, so they
 * ask for formats no CAMetalLayer may hold -- backboardd asked 55 times in one boot for
 * MTLPixelFormat 500, which is MTLPixelFormatYCBCR8_420_2P. Three of the four pass plane 0
 * unconditionally; ::update_surface passes a plane variable (csel x4 at 0x1883163d8), so the
 * compositor does per-plane imports when it gets that far. This file therefore serves "a
 * texture over an IOSurface" for whoever asks, and refuses what one linear plane cannot be.
 *
 * HOW THE MEMORY GETS HERE. Four layers, and all four now exist:
 *
 *   the kernel     kRedwoodSurface (0x7e) turns an IOSurface into a GPU buffer and reports
 *                  its size, offset, row length and geometry (RedwoodGPU.cpp:821)
 *   the DRM shim   drmPrimeFDToHandle(fd, surface_id, &handle) -- the ID goes where Linux
 *                  puts a dmabuf descriptor (tools/mesa/redwood-drm/redwood_drm.c:189)
 *   freedreno      fd_bo_from_dmabuf_drm takes it ("what comes in here is an IOSurface, and
 *                  the file descriptor its ID"), and fd_resource_from_handle lays a resource
 *                  over it with one pitch
 *   the contract   rwa_texture_from_surface (tools/mesa/air/air_metal.h)
 *
 * The last of those is new, and it is still looked up rather than linked: an image built
 * before it existed has a library without the symbol, and then this file refuses BY NAME
 * instead of returning a bare nil. That distinction is not tidiness -- get_unused_drawable
 * does NOT test the texture it gets back (no branch between the call at 0x18837d220 and the
 * store at 0x18837d224), so a nil here becomes a CAMetalDrawable whose -texture is nil, and
 * the application finds out later, somewhere else, as an assertion about a nil colour
 * attachment. A sentence at the moment of the refusal names what was missing.
 *
 * WHO OWNS THE BUFFER, because this was wrong once. The buffer the kernel makes for a
 * surface belongs to freedreno's table from the moment it imports it, and
 * rwa_texture_destroy gives it back. So this file only DESCRIBES the surface (which
 * allocates nothing) and leaves the taking to the library. Taking it here as well would be a
 * second buffer over the same memory on a second user client -- a waste rather than a fault,
 * but closing it would then look like closing the library's.
 */

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "redwood_drawable.h"
#include "m2m.h"
#include "rw_api99a_pure.h"     /* y2s99a (subrect) */

/* y2s99a: in redwoodmetal.m (feature 1 = subrect) */
int rw_api99a_on(int feature);
void rw_api99a_count(int feature, int ok, const char *what);

/*
 * The two requests this file needs, by the numbers the kernel answers them on
 * (xnu/files/iokit/Kernel/RedwoodGPU.cpp:78-95): kRedwoodSurface is this port's own and
 * kDrmGemClose is Linux's. Repeated here rather than shared for the reason the structs are
 * repeated, and it is two lines.
 */
#define RWD_REQUEST_SURFACE   0x7e
#define RWD_REQUEST_GEM_CLOSE 0x09

/* A sentence into a caller's buffer, once, with no dependency on the caller having one. */
static void
rwd_say(char *why, size_t why_size, const char *format, ...)
{
	va_list arguments;

	if (why == NULL || why_size == 0) {
		return;
	}
	va_start(arguments, format);
	vsnprintf(why, why_size, format, arguments);
	va_end(arguments);
}

int
rwd_gpu_open(io_connect_t *connection, char *why, size_t why_size)
{
	io_service_t service = IOServiceGetMatchingService(0, IOServiceMatching("RedwoodGPU"));

	if (service == 0) {
		/* the Exynos 990 phone's Mali-G77 (iokit/Kernel/RedwoodMali.cpp): the same kRedwoodSurface (0x7e)
		 * request, with the same structure, and the same GEM close (0x09) */
		service = IOServiceGetMatchingService(0, IOServiceMatching("RedwoodMali"));
	}
	kern_return_t result;

	if (connection == NULL) {
		rwd_say(why, why_size, "rwd_gpu_open was given nowhere to put the connection");
		return -1;
	}
	*connection = 0;
	if (service == IO_OBJECT_NULL) {
		rwd_say(why, why_size, "no RedwoodGPU service: this kernel was booted without "
		    "gpu=1, so there is no GPU to give a surface to");
		return -1;
	}
	result = IOServiceOpen(service, mach_task_self(), 0, connection);
	IOObjectRelease(service);
	if (result != KERN_SUCCESS) {
		*connection = 0;
		rwd_say(why, why_size, "RedwoodGPU refused a user client: %#x", result);
		return -1;
	}
	return 0;
}

void
rwd_gpu_close(io_connect_t connection)
{
	if (connection != 0) {
		IOServiceClose(connection);
	}
}

/*
 * The connection a drawable's buffer lives on, opened once and never closed.
 *
 * This is not a convenience, it is the buffer's lifetime. A buffer made by kRedwoodSurface
 * belongs to the user client that asked for it, and IOSurfaceRootUserClient's and
 * RedwoodGPUUserClient's clientClose free what their client held -- so a connection closed
 * while a texture still refers to its buffer takes the texture's memory with it. One
 * connection for the process, kept for as long as the process has textures, is the only
 * lifetime that is right, and it costs one Mach port.
 *
 * 0 when there is no GPU service, which is a kernel booted without gpu=1 and the ordinary
 * case rather than a fault; the sentence is printed once, by whoever asked first.
 */
static io_connect_t
rwd_shared_connection(void)
{
	static io_connect_t connection;
	static int opened;

	if (!opened) {
		char why[256] = { 0 };

		opened = 1;
		if (rwd_gpu_open(&connection, why, sizeof(why)) != 0) {
			connection = 0;
			m2m_log('E', "SURF", "drawable: the GPU service: %s", why);
		}
	}
	return connection;
}

/*
 * One kRedwoodSurface call. The kernel takes the struct in and hands the same struct back
 * filled (RedwoodGPU.cpp:1283 and the common copy at the end of externalMethod), so in and
 * out are one buffer of one size, and asking for fewer bytes back than were sent loses the
 * tail of the answer -- which is the one way to misuse this request.
 */
static int
rwd_surface_call(io_connect_t connection, struct rwd_surface *surface, const char *what,
		 char *why, size_t why_size)
{
	struct rwd_surface reply;
	size_t size = sizeof(reply);
	kern_return_t result;

	if (connection == 0) {
		rwd_say(why, why_size, "no connection to RedwoodGPU to %s a surface with", what);
		return -1;
	}
	result = IOConnectCallStructMethod(connection, RWD_REQUEST_SURFACE, surface,
	    sizeof(*surface), &reply, &size);
	if (result != KERN_SUCCESS) {
		/*
		 * Two of these answers mean something nameable and the rest do not, so only
		 * those two are translated. kIOReturnNotFound (0xe00002f0) is a surface ID the
		 * kernel's table does not hold -- a surface from another process that was never
		 * made shareable, or one already released. kIOReturnUnsupported (0xe00002c7) has
		 * exactly one cause here (RedwoodGPU.cpp:830): the surface was built around
		 * memory the process already had, which is neither whole pages nor in rows the
		 * GPU takes.
		 */
		rwd_say(why, why_size, "the kernel would not %s surface %u: %#x%s", what,
		    surface->surface, result,
		    result == kIOReturnUnsupported
			? " (that surface wraps memory of the process's own; the GPU cannot "
			  "take such memory as a buffer)"
			: result == kIOReturnNotFound
			    ? " (no surface of that ID in the kernel's table)"
			    : "");
		return -1;
	}
	if (size < sizeof(reply)) {
		rwd_say(why, why_size, "the kernel answered %zu bytes about surface %u, not %zu",
		    size, surface->surface, sizeof(reply));
		return -1;
	}
	*surface = reply;
	return 0;
}

int
rwd_surface_describe(io_connect_t connection, uint32_t surfaceID, struct rwd_surface *surface,
		     char *why, size_t why_size)
{
	if (surface == NULL) {
		rwd_say(why, why_size, "rwd_surface_describe was given nowhere to answer into");
		return -1;
	}
	memset(surface, 0, sizeof(*surface));
	surface->surface = surfaceID;
	surface->handle = RWD_DESCRIBE;
	return rwd_surface_call(connection, surface, "describe", why, why_size);
}

int
rwd_surface_take(io_connect_t connection, uint32_t surfaceID, struct rwd_surface *surface,
		 char *why, size_t why_size)
{
	if (surface == NULL) {
		rwd_say(why, why_size, "rwd_surface_take was given nowhere to answer into");
		return -1;
	}
	memset(surface, 0, sizeof(*surface));
	surface->surface = surfaceID;
	surface->handle = 0;
	if (rwd_surface_call(connection, surface, "take", why, why_size) != 0) {
		return -1;
	}
	if (surface->handle == 0) {
		rwd_say(why, why_size, "the kernel took surface %u and gave back buffer 0, "
		    "which is no buffer", surfaceID);
		return -1;
	}
	return 0;
}

int
rwd_surface_give_back(io_connect_t connection, uint32_t handle, char *why, size_t why_size)
{
	struct {
		uint32_t handle, pad;
	} close = { handle, 0 };
	kern_return_t result;

	if (connection == 0 || handle == 0) {
		return 0;                 /* nothing was taken, so nothing is owed */
	}
	result = IOConnectCallStructMethod(connection, RWD_REQUEST_GEM_CLOSE, &close,
	    sizeof(close), NULL, NULL);
	if (result != KERN_SUCCESS) {
		rwd_say(why, why_size, "the kernel would not take buffer %u back: %#x", handle,
		    result);
		return -1;
	}
	return 0;
}

uint32_t
rwd_surface_id(void *iosurface)
{
	static uint32_t (*get_id)(void *);
	static int looked;

	if (iosurface == NULL) {
		return 0;
	}
	if (!looked) {
		/*
		 * RTLD_DEFAULT and not a link: this driver is loaded as a bundle into whatever
		 * process asks Metal, and a hard dependency on IOSurface.framework would fail
		 * that load rather than fail this one call. A process that has a CAMetalLayer
		 * has the framework loaded already -- CAMetalDrawable's own private structure
		 * holds an `__IOSurface *` -- so the symbol is there exactly when it is wanted.
		 */
		*(void **)&get_id = dlsym(RTLD_DEFAULT, "IOSurfaceGetID");
		looked = 1;
		if (get_id == NULL) {
			m2m_log('E', "SURF", "drawable: this process has no IOSurfaceGetID, so a "
			        "surface cannot be named to the kernel");
		}
	}
	return get_id != NULL ? get_id(iosurface) : 0;
}

int
rwd_surface_fits_adreno(const struct rwd_surface *surface, unsigned bytes_per_pixel,
			char *why, size_t why_size)
{
	/*
	 * a6xx's minimum pitchalign is 6 -- fd_resource_from_handle sets
	 * MAX2(fdl_cpp_shift + log2(gmem_align_w), 6) and gmem_align_w is 16 for this
	 * generation, which gives 2 + 4 = 6 for a four-byte pixel -- so a row has to be a
	 * multiple of 64 bytes. Named as the number rather than the shift, because the number
	 * is what the kernel's MINISURF_ROW_ALIGN also is, and the agreement between the two
	 * trees is the whole reason this is expected to pass.
	 */
	const unsigned align = 64;
	uint64_t least;

	if (surface == NULL || bytes_per_pixel == 0) {
		rwd_say(why, why_size, "rwd_surface_fits_adreno needs a surface and a pixel size");
		return -1;
	}
	if (surface->planes > 2 || (surface->planes == 2 && surface->planeRowBytes[1] == 0)) {
		rwd_say(why, why_size, "surface %u has %u planes; a drawable's texture is one "
		    "plane and this driver imports no others", surface->surface, surface->planes);
		return -1;
	}
	if (surface->planes == 2) {
		/*
		 * y2s36: two planes (4:2:0, 'CbCr' in the second) are checked plane by plane. The surface's own
		 * rowBytes says nothing about them: IOSurface answers a made-up number for a planar surface (the
		 * camera preview, 1128x1504 '420f', says 1732 -- its allocation divided by its height -- over two
		 * planes of 1152 bytes a row), and checking that number refused every such surface before
		 * rwa_texture_from_surface_planes was ever asked. What the import takes is each plane's offset and
		 * row: the compiler library wants the luma row a multiple of 64 (texture_of_shape), Panfrost
		 * wants rows and offsets multiples of 16 for this format and then rounds an offset UP to 64
		 * without saying so (pan_image_layout_init), which would show the picture shifted -- so an
		 * offset that is not a multiple of 64 is refused here, by name.
		 */
		uint32_t chroma_width = (surface->width + 1) / 2, chroma_height = (surface->height + 1) / 2;
		uint64_t least_plane[2] = { (uint64_t)surface->width * bytes_per_pixel,
					    (uint64_t)chroma_width * 2 * bytes_per_pixel };
		uint64_t rows[2] = { surface->height, chroma_height };

		for (unsigned i = 0; i < 2; i++) {
			if (surface->planeRowBytes[i] < least_plane[i] || surface->planeRowBytes[i] % align != 0 ||
			    surface->planeOffset[i] % align != 0 ||
			    surface->size < surface->planeOffset[i] + (uint64_t)surface->planeRowBytes[i] * rows[i]) {
				rwd_say(why, why_size, "surface %u (%ux%u, two planes, %llu bytes) has a %s plane the "
				    "GPU cannot import in place: it needs a row of at least %llu bytes, rows and "
				    "offsets that are multiples of %u, and %llu rows inside the surface; the planes "
				    "are %u bytes a row from offset %u and %u bytes a row from offset %u (the "
				    "surface's own %u bytes a row is not used for a planar surface)",
				    surface->surface, surface->width, surface->height,
				    (unsigned long long)surface->size, i == 0 ? "luma" : "chroma",
				    (unsigned long long)least_plane[i], align, (unsigned long long)rows[i],
				    surface->planeRowBytes[0], surface->planeOffset[0], surface->planeRowBytes[1],
				    surface->planeOffset[1], surface->rowBytes);
				return -1;
			}
		}
		return 0;
	}
	least = (uint64_t)surface->width * bytes_per_pixel;
	if (surface->rowBytes < least) {
		rwd_say(why, why_size, "surface %u has %u bytes a row, and %ux%u pixels of %u "
		    "bytes need at least %llu", surface->surface, surface->rowBytes,
		    surface->width, surface->height, bytes_per_pixel,
		    (unsigned long long)least);
		return -1;
	}
	if (surface->rowBytes % align != 0) {
		rwd_say(why, why_size, "surface %u has %u bytes a row, which is not a multiple "
		    "of %u; freedreno refuses such an import (fd_resource_from_handle), and the "
		    "kernel's own surfaces are aligned to %u (MINISURF_ROW_ALIGN)",
		    surface->surface, surface->rowBytes, align, align);
		return -1;
	}
	if (surface->size < (uint64_t)surface->rowBytes * surface->height + surface->offset) {
		rwd_say(why, why_size, "surface %u is %llu bytes, and %u rows of %u bytes from "
		    "offset %u need %llu", surface->surface, (unsigned long long)surface->size,
		    surface->height, surface->rowBytes, surface->offset,
		    (unsigned long long)((uint64_t)surface->rowBytes * surface->height +
		    surface->offset));
		return -1;
	}
	return 0;
}

/*
 * y2s41: whether plane `plane` (0 luma, 1 chroma) of a two-plane 4:2:0 surface can be a width x height texture in place: the
 * plane's own size (the chroma plane is half the surface each way, rounded up), a row and an offset that are multiples of 64
 * (what the import takes, and what it would otherwise round an offset up to without saying), and the plane inside the surface.
 */
static int
rwd_plane_fits_gpu(const struct rwd_surface *surface, unsigned plane, unsigned texel, unsigned width, unsigned height, char *why,
		   size_t why_size)
{
	const unsigned align = 64;

	if (surface == NULL || plane > 1 || surface->planes != 2) {
		rwd_say(why, why_size, "rwd_plane_fits_gpu needs a two-plane surface and plane 0 or 1");
		return -1;
	}
	uint32_t plane_width = plane ? (surface->width + 1) / 2 : surface->width;
	uint32_t plane_height = plane ? (surface->height + 1) / 2 : surface->height;
	uint64_t least = (uint64_t)plane_width * (plane ? 2 : 1);
	uint32_t row = surface->planeRowBytes[plane], offset = surface->planeOffset[plane];

	/* y2s43: SEVERAL ELEMENTS OF THE PLANE IN ONE TEXEL. VideoToolbox's Metal transfer writes a '420f' destination through
	 * R32Uint (53) on BOTH planes: four luma bytes, or two CbCr pairs, packed into each texel by the kernel itself
	 * (pack_float_to_unorm4x8), so the texture is the plane's bytes at a quarter (half) of the plane's width -- run q10hw: plane 0
	 * of a 1128x1504 surface asked for as 282x1504 of format 53, refused here 2048 times, each one a preview frame dropped
	 * (-12914, 'PixelTransferFailure') while the zoom moved. VideoToolbox divides the plane's width by the number of elements
	 * (0x194f62d48, an integer division), so that is the width taken, and no other. */
	unsigned elem = plane ? 2u : 1u;

	if (texel > elem && texel % elem == 0) {
		plane_width = (uint32_t)((uint64_t)plane_width * elem / texel);
	}
	if (width != plane_width || height != plane_height) {
		rwd_say(why, why_size, "plane %u of surface %u (%ux%u) is %ux%u in texels of %u byte%s and the descriptor asks for %ux%u",
		    plane, surface->surface, surface->width, surface->height, plane_width, plane_height, texel, texel == 1 ? "" : "s",
		    width, height);
		return -1;
	}
	if (row < least || row % align != 0 || offset % align != 0 ||
	    surface->size < (uint64_t)offset + (uint64_t)row * plane_height) {
		rwd_say(why, why_size, "plane %u of surface %u (%ux%u texels, %llu bytes) cannot be a texture in place: it is %u "
		    "bytes a row from offset %u, and needs a row of at least %llu, rows and offsets that are multiples of %u, and "
		    "%u rows inside the surface", plane, surface->surface, plane_width, plane_height,
		    (unsigned long long)surface->size, row, offset, (unsigned long long)least, align, plane_height);
		return -1;
	}
	return 0;
}

/*
 * Metal's pixel format, CoreAnimation's FourCC and the size of a pixel, in one table.
 *
 * The single-plane entries are read out of -[CAMetalLayer setPixelFormat:] (0x1882a3d38),
 * which is where that correspondence is decided for a drawable: it accepts 10/11, 70/71,
 * 80/81, 90, 94, 115 and 550-555, raises an NSException on anything else, and composes the
 * surface's FourCC from the number.
 *
 * But a drawable is NOT the only caller, and assuming it was cost a wrong sentence in a real
 * boot (Metal notes 10.46, "format 500"). Five call sites in QuartzCore send
 * -newTextureWithDescriptor:iosurface:plane:, and four of them are the window server's Metal
 * compositor rather than the drawable path: CA::OGL::MetalContext::set_destination_iosurface
 * (0x188302550), ::update_surface (0x1883163e0), ::get_surface (0x18833e724) and
 * ::create_surface_from_iosurface (0x18835e55c). Those composite whatever surface a client
 * handed the window server, so they ask for formats no CAMetalLayer may hold -- and the
 * authority for what this import can serve is therefore NOT CAMetalLayer's list but what one
 * linear plane can be. Hence the refusals below say what they are refusing.
 *
 * Two families are in the table and then refused, each for its own measured reason:
 *
 *   the XR formats (550-555, and 94)   the kernel substitutes 'w30r', 'l10r', 'w30R' and
 *       'b3a8' with 'BGRA' of the same geometry (MiniIOSurface.cpp:466-479), so a ten-bit
 *       surface asked for comes back eight-bit and a texture made to the asked-for format
 *       would compute wrong colours into it rather than fail;
 *   the YCbCr formats (500-629)        two planes, and nothing on this port has ever carried
 *       a second plane. See rwd_format_of_metal for the three things that would be needed.
 */
struct rwd_format {
	unsigned metal;
	uint32_t fourcc;
	unsigned bytes_per_pixel;
	int substituted;      /* the kernel turns this FourCC into something else */
	const char *name;
};

static const struct rwd_format rwd_formats[] = {
	{  10, 'A008', 1, 0, "MTLPixelFormatR8Unorm" },
	{  10, 'L008', 1, 0, "MTLPixelFormatR8Unorm" },
	{  11, 'A008', 1, 0, "MTLPixelFormatR8Unorm_sRGB" },
	{  11, 'L008', 1, 0, "MTLPixelFormatR8Unorm_sRGB" },
	/* luminance + alpha, one byte each: SpringBoard's icon and label layers (25 October: 146
	 * "Failed to allocate MTLTexture from IOSurface" in one boot, the layers simply missing) */
	{  30, 'LA08', 2, 0, "MTLPixelFormatRG8Unorm" },
	{  30, '2C08', 2, 0, "MTLPixelFormatRG8Unorm" },
	{  70, 'RGBA', 4, 0, "MTLPixelFormatRGBA8Unorm" },
	{  71, 'RGBA', 4, 0, "MTLPixelFormatRGBA8Unorm_sRGB" },
	/* y2s41: an RGBA8 texture over a surface that calls itself 'BGRA'. Not a swap of channels: the texture is the surface's
	 * bytes read as R, G, B, A in the order they lie, which is what Metal's own drivers give for it (the method takes a
	 * descriptor and a surface and compares nothing). CMCapture makes image pyramids this way in mediaserverd -- 36 small
	 * 'BGRA' surfaces a session, each asked for as format 70 -- and every one was refused by name. */
	{  70, 'BGRA', 4, 0, "MTLPixelFormatRGBA8Unorm" },
	{  71, 'BGRA', 4, 0, "MTLPixelFormatRGBA8Unorm_sRGB" },
	/* y2s41: the integer views of one- and two-byte texels, for a plane of a two-plane surface (and the surfaces of their own) */
	{  13, 'L008', 1, 0, "MTLPixelFormatR8Uint" },
	{  33, '2C08', 2, 0, "MTLPixelFormatRG8Uint" },
	{  80, 'BGRA', 4, 0, "MTLPixelFormatBGRA8Unorm" },
	{  81, 'BGRA', 4, 0, "MTLPixelFormatBGRA8Unorm_sRGB" },
	{  90, 'R10k', 4, 0, "a ten-bit RGB format" },
	{  94, 'l10r', 4, 1, "a ten-bit RGB format" },
	{ 115, 'RGhA', 8, 0, "MTLPixelFormatRGBA16Float" },
	/* two planes, 4:2:0, full and video range: the luma plane's one byte a pixel is what is
	 * checked here; the chroma plane is the kernel's to describe (planeOffset/planeRowBytes) */
	{ 500, '420f', 1, 0, "MTLPixelFormatYCBCR8_420_2P" },
	{ 500, '420v', 1, 0, "MTLPixelFormatYCBCR8_420_2P" },
	/* y2s42: the same two planes as one texture with no conversion implied (Metal's private 540): what VideoToolbox's Metal
	 * transfer reads a '420f'/'420v' source through. Imported in place exactly as 500 is; sampling gives Y, Cb, Cr. */
	{ 540, '420f', 1, 0, "MTLPixelFormatRGB8_420_2P" },
	{ 540, '420v', 1, 0, "MTLPixelFormatRGB8_420_2P" },
	{ 550, 'b3a8', 4, 1, "a ten-bit XR format" },
	{ 551, 'b3a8', 4, 1, "a ten-bit XR format" },
	{ 552, 'w40a', 4, 1, "a ten-bit XR format" },
	{ 553, 'w40a', 4, 1, "a ten-bit XR format" },
	{ 554, 'w30r', 4, 1, "a ten-bit XR format" },
	{ 555, 'w30r', 4, 1, "a ten-bit XR format" },
};

/*
 * y2s42: THE PLAIN FORMATS, BY SIZE. The table above is the formats whose surface CoreAnimation names; this is every other format
 * of one, two or four channels of 8, 16 or 32 bits that the compiler library has (rwa_pixel_formats) and the Mali can lay over
 * linear memory, with the size of its texel. Such a texture is served over a plane whose element is the same size: the texture is
 * the plane's bytes read in the format asked, which is all -newTextureWithDescriptor:iosurface:plane: means on Apple's own drivers
 * (CoreVideo's CVMTLTextureCreate, 0x1a1edb704, hands the format over with the surface and compares nothing), and it is how the
 * frameworks use it: VideoToolbox writes a 'BGRA' surface through R32Uint (53) and the chroma plane of '420f' through R16Uint
 * (23), CMCapture renders R16Float (25) into 'L00h'.
 *
 * NOT here, on purpose: 70/71/80/81 (four channels of a byte), which stay with the FourCCs the table above names for them -- a
 * surface of the other channel order under one of those would show swapped colours and not fail -- and the packed, depth,
 * compressed and ten-bit formats.
 */
static unsigned
rwd_plain_format_size(unsigned metal)
{
	switch (metal) {
	case 1: case 10: case 11: case 12: case 13: case 14:                    /* A8, R8 */
		return 1;
	case 20: case 22: case 23: case 24: case 25:                            /* R16 */
	case 30: case 31: case 32: case 33: case 34:                            /* RG8 */
		return 2;
	case 53: case 54: case 55:                                              /* R32 */
	case 60: case 62: case 63: case 64: case 65:                            /* RG16 */
	case 72: case 73: case 74:                                              /* RGBA8 Snorm, Uint, Sint */
		return 4;
	case 103: case 104: case 105:                                           /* RG32 */
	case 110: case 112: case 113: case 114: case 115:                       /* RGBA16 */
		return 8;
	case 123: case 124: case 125:                                           /* RGBA32 */
		return 16;
	default:
		return 0;
	}
}

/* The size of one element of a single-plane surface of `fourcc`, for the FourCCs that are plain arrays of such elements; 0 for
 * the rest (which are then served only as the table above says). CoreVideo's own list of them (CVPixelBuffer.h). */
static unsigned
rwd_plain_fourcc_size(uint32_t fourcc)
{
	switch (fourcc) {
	case 'L008': case 'A008':
		return 1;
	case 'L016': case 'L010': case 'L00h': case '2C08': case 'LA08': case 'hdis': case 'hdep':
		return 2;
	case 'L00f': case '2C16': case '2C0h': case 'BGRA': case 'RGBA': case 'fdis': case 'fdep':
		return 4;
	case '2C0f': case 'RGhA': case 'l64r':
		return 8;
	case 'RGfA':
		return 16;
	default:
		return 0;
	}
}

/*
 * y2s49: whether a texture of `texel`-byte texels, want_w x want_h, may be laid over a one-plane surface of `elem`-byte elements
 * (both plain: not 0), in place. The texture's rows are the surface's rows -- the same height, the surface's row length -- and a
 * row of the texture is the LEADING bytes of the surface's row: no more of them than the surface's own pixels hold, and a whole
 * number of the surface's elements or of the texture's texels whichever is the larger (so nothing is cut in the middle).
 * These are the cases of the still-image noise reduction, which keeps one piece of memory under several formats
 * (-[FigMetalContext rebindTex:format:usage:plane:xFactor:], 0x1c789de70: the asked format at the texture's width / xFactor):
 *   -[PyramidStorage setPixelBufferFloat16:...]: RG16Float (65) at half the width of the 'L00h' luma; R32Uint (53) over '2C0h';
 *   +[AMBNRBuffers bindFP16PyramidToU8:pyr_u8:metal:] (0x1f20606ec): R8Unorm (10) at the width, RG8Unorm (30) at the width and at
 *   half the width, R16Uint (23) at the width -- over 'L00h' (2-byte) and '2C0h' (4-byte) surfaces;
 *   and VideoToolbox's R32Uint over 'L008' and the like (y2s43).
 */
static int
rwd_plain_over_surface(unsigned elem, unsigned texel, unsigned surface_w, unsigned surface_h, unsigned row_bytes, unsigned want_w,
		       unsigned want_h)
{
	uint64_t bytes = (uint64_t)want_w * texel;

	if (elem == 0 || texel == 0 || want_w == 0 || want_h != surface_h) {
		return 0;
	}
	if (bytes > (uint64_t)surface_w * elem || bytes > row_bytes) {
		return 0;
	}
	return texel % elem == 0 || elem % texel == 0;
}

/*
 * webxr1 (14 November): the extended-range formats 550-555 over the 'BGRA' surface the kernel makes of 'w30r' / 'b3a8' / 'w40a'.
 * The compiler library serves them as eight bits a channel (BGRA8 for 550-553, BGRX8 for 554/555), which is what that memory is,
 * so writer and reader agree. WebKit's page tiles are 'w30r' drawn as 554 in com.apple.WebKit.GPU: refused, Safari pages black.
 * Not while /var/db/nord2-metal/no-webxr exists (read once per process): the old refusal.
 */
static int
rwd_xr8_on(void)
{
	static int on = -1;

	if (on < 0) {
		on = access("/var/db/nord2-metal/no-webxr", F_OK) != 0;
		m2m_log('I', "WEBXR", "webxr1 %s: Metal formats 550-555 %s over 'BGRA' surfaces (/var/db/nord2-metal/no-webxr %s)",
		    on ? "on" : "off", on ? "served as 8-bit BGRA/BGRX" : "refused", on ? "absent" : "present");
	}
	return on;
}

static int
rwd_is_xr(unsigned metal)
{
	return metal >= 550 && metal <= 555;
}

/* Whether a surface of `fourcc` may stand under Metal format `metal` (a format may have several). */
static int
rwd_fourcc_serves(unsigned metal, uint32_t fourcc)
{
	if (rwd_is_xr(metal) && fourcc == 'BGRA' && rwd_xr8_on()) {
		return 1;        /* webxr1 */
	}
	/*
	 * gfxall13 (xrsurf): a surface that still SAYS 'w30r' (BGR10_XR, 4 bytes a pixel, opaque) under Metal 554/555, or 'b3a8' (its first plane,
	 * 4 bytes a pixel) under 550/551: com.apple.WebKit.GPU renders Safari's page tiles into such surfaces (the kernel's 'BGRA' substitution
	 * does not reach them) and backboardd samples them -- both through webxr's 8-bit alias, so writer and reader read the same bytes the same
	 * way. Refused before: 'surface N is 'w30r' and Metal format 554 is 'BGRA'', every tile, white pages. Off with /var/db/nord2-metal/no-xrsurf.
	 */
	if (rwd_is_xr(metal) && rwd_xr8_on() && ((fourcc == 'w30r' && metal >= 554) || (fourcc == 'b3a8' && metal <= 551))) {
		static int on = -1;

		if (on < 0) {
			on = access("/var/db/nord2-metal/no-xrsurf", F_OK) != 0;
			m2m_log('I', "WEBXR", "gfxall13 xrsurf %s: Metal 554/555 over 'w30r' and 550/551 over 'b3a8' surfaces %s (/var/db/nord2-metal/no-xrsurf %s)",
			    on ? "on" : "off", on ? "served as 8-bit BGRX/BGRA in place" : "refused", on ? "absent" : "present");
		}
		if (on) {
			return 1;
		}
	}
	for (size_t i = 0; i < sizeof(rwd_formats) / sizeof(rwd_formats[0]); i++) {
		if (rwd_formats[i].metal == metal && rwd_formats[i].fourcc == fourcc && !rwd_formats[i].substituted) {
			return 1;
		}
	}
	/* y2s42: a plain format over a plain surface of the same element size */
	return rwd_plain_format_size(metal) != 0 && rwd_plain_format_size(metal) == rwd_plain_fourcc_size(fourcc);
}

int
rwd_format_of_metal(unsigned metal_pixel_format, uint32_t *fourcc, unsigned *bytes_per_pixel,
		    char *why, size_t why_size)
{
	/* webxr1: an XR format is the kernel's 'BGRA' of the same geometry, four bytes a pixel, and is served as such */
	if (rwd_is_xr(metal_pixel_format) && rwd_xr8_on()) {
		if (fourcc != NULL) {
			*fourcc = 'BGRA';
		}
		if (bytes_per_pixel != NULL) {
			*bytes_per_pixel = 4;
		}
		return 0;
	}
	for (size_t i = 0; i < sizeof(rwd_formats) / sizeof(rwd_formats[0]); i++) {
		const struct rwd_format *f = &rwd_formats[i];

		if (f->metal != metal_pixel_format) {
			continue;
		}
		if (f->substituted) {
			rwd_say(why, why_size, "pixel format %u is %s, whose surface this "
			    "kernel makes as ordinary BGRA of the same geometry "
			    "(MiniIOSurface.cpp substitutes '%c%c%c%c'); a texture of the asked-"
			    "for format over such a surface would write wrong colours rather "
			    "than fail, so it is refused", metal_pixel_format, f->name,
			    (char)(f->fourcc >> 24), (char)(f->fourcc >> 16),
			    (char)(f->fourcc >> 8), (char)f->fourcc);
			return -1;
		}
		if (fourcc != NULL) {
			*fourcc = f->fourcc;
		}
		if (bytes_per_pixel != NULL) {
			*bytes_per_pixel = f->bytes_per_pixel;
		}
		return 0;
	}
	/* y2s42: a plain format the table does not name (rwd_plain_format_size): no FourCC of its own -- any plain surface whose
	 * element is this size serves it (rwd_fourcc_serves) -- and the size of its texel */
	if (rwd_plain_format_size(metal_pixel_format) != 0) {
		if (fourcc != NULL) {
			*fourcc = 0;
		}
		if (bytes_per_pixel != NULL) {
			*bytes_per_pixel = rwd_plain_format_size(metal_pixel_format);
		}
		return 0;
	}
	/*
	 * The YCbCr family, named rather than counted. Metal handles 500 to 629 as one block --
	 * `sub x8, x20, #0x1f4; cmp x8, #0x81` at 0x181a1f614 in the function behind
	 * MTLPixelFormatGetName -- and the case for offset 0 of its jump table stores the name
	 * at 0x181b643bf, which __TEXT,__cstring spells MTLPixelFormatYCBCR8_420_2P. So 500 is
	 * eight-bit YCbCr, 4:2:0, TWO PLANES, and the window server really does ask for it: 55
	 * times in one boot, all for one surface, out of backboardd.
	 */
	if (metal_pixel_format >= 500 && metal_pixel_format <= 629) {
		/* Kept short enough to fit a 512-byte buffer whole: a refusal that is cut off
		 * mid-reason is a refusal nobody can act on, which is the failure this file
		 * exists to avoid. The three things it needs are in docs/metal.md 10.46. */
		/* y2s42: the sentence said what was missing when nothing of this family was served. Two of them are now (500 and
		 * 540, eight-bit 4:2:0 in two planes, over '420f' and '420v'), so it says which the rest are. */
		rwd_say(why, why_size, "pixel format %u is one of Metal's private multi-plane formats (500-629), and of those this "
		    "import serves 500 (YCBCR8_420_2P) and 540 (RGB8_420_2P), which are the two planes of a '420f' or '420v' "
		    "surface; 4:2:2, 4:4:4, ten-bit and packed layouts have no texture on this GPU yet. Refused whole rather than "
		    "served as its luma plane, which would look like a bug elsewhere", metal_pixel_format);
		return -1;
	}
	/*
	 * And the catch-all, which deliberately does NOT say "so it did not come from a
	 * drawable" any more. It did not, and saying so was true and useless: four of the five
	 * callers of this method are not the drawable path at all (see the table above), so the
	 * interesting fact is what this import can lay over a linear surface, not which caller
	 * it was.
	 */
	/*
	 * The list of what IS served is built from the table rather than written out beside it.
	 * That is not neatness: a sentence listing numbers next to a table of numbers drifts the
	 * moment one of them changes, and this very sentence had 90 in the wrong half of it
	 * before the table was read again.
	 */
	char served[160] = "", refused[160] = "";
	size_t sn = 0, rn = 0;

	for (size_t i = 0; i < sizeof(rwd_formats) / sizeof(rwd_formats[0]); i++) {
		char *into = rwd_formats[i].substituted ? refused : served;
		size_t *at = rwd_formats[i].substituted ? &rn : &sn;
		int wrote = snprintf(into + *at, (rwd_formats[i].substituted ? sizeof(refused)
								            : sizeof(served)) - *at,
		    "%s%u", *at ? ", " : "", rwd_formats[i].metal);

		if (wrote > 0) {
			*at += (size_t)wrote;
		}
	}
	rwd_say(why, why_size, "pixel format %u is not one this import can lay over a single "
	    "linear plane. It serves %s, and the plain one-, two- and four-channel formats over a surface whose "
	    "element is the same size; it refuses %s because this kernel substitutes BGRA for those FourCCs",
	    metal_pixel_format, served, refused);
	return -1;
}

/*
 * The import call of the compiler library, if it has one.
 *
 * It is looked up rather than linked for the same reason src/redwoodmetal.m dlopens the
 * library at all, and with RTLD_NOLOAD so that this file never loads a second copy: dyld
 * uniques images by install_name, so a handle asked for this way is the one the driver
 * already has, or nothing when the driver never got one.
 *
 * The signature is tools/mesa/air/air_metal.h's, field for field: the shape the caller wants,
 * the surface by its ID (which is where freedreno's import takes it --
 * drmPrimeFDToHandle(fd, surface, &handle) in tools/mesa/redwood-drm/redwood_drm.c:189), and
 * the row length and offset the kernel reported, because fd_resource_from_handle takes those
 * from the caller and does not ask the kernel again.
 */
typedef struct rwa_texture *(*rwd_import_call)(struct rwa_device *device,
					       const struct rwd_texture_shape *shape,
					       int surface_id, unsigned offset,
					       unsigned row_bytes, char *why, size_t why_size);


typedef struct rwa_texture *(*rwd_import_planes_call)(struct rwa_device *, const struct rwd_texture_shape *,
    int, unsigned, unsigned, unsigned, unsigned, char *, size_t);
static rwd_import_planes_call rwd_import_planes;
static rwd_import_call
rwd_import(void)
{
	static rwd_import_call import;
	static int looked;

	if (!looked) {
		void *library = dlopen("/System/Library/Extensions/Nord2Metal.bundle/libredwoodair.dylib", RTLD_NOW | RTLD_NOLOAD);

		looked = 1;
		if (library != NULL) {
			*(void **)&import = dlsym(library, "rwa_texture_from_surface");
			*(void **)&rwd_import_planes = dlsym(library, "rwa_texture_from_surface_planes");
		}
		m2m_log(import != NULL ? 'I' : 'E', "SURF", "drawable: the compiler library %s, and %s "
		        "rwa_texture_from_surface",
		    library != NULL ? "is loaded in this process" : "is not loaded here",
		    import != NULL ? "has" : "has NOT");
	}
	return import;
}

int
rwd_import_available(void)
{
	return rwd_import() != NULL;
}

struct rwa_texture *
rwd_texture_for_surface(struct rwa_device *mesa_device, void *iosurface, unsigned long plane,
			const struct rwd_texture_shape *want, struct rwd_surface *took,
			char *why, size_t why_size)
{
	struct rwd_surface surface;
	io_connect_t connection;
	rwd_import_call import;
	struct rwa_texture *texture;
	unsigned bytes_per_pixel = 0;
	uint32_t fourcc = 0, id;

	if (took != NULL) {
		memset(took, 0, sizeof(*took));
	}
	if (mesa_device == NULL || want == NULL) {
		rwd_say(why, why_size, "a drawable's texture needs the compiler library's device "
		    "and a shape to make it in");
		return NULL;
	}
	/*
	 * Plane 0 and no other, said before anything is taken. CoreAnimation asks for plane 0
	 * always on this path -- allocate_drawable_texture passes x4 = 0 at 0x1884b3590 -- so a
	 * plane of anything else is an application's own IOSurface and not a drawable's, and it
	 * would need a per-plane geometry this driver does not read.
	 */
	if (plane > 1) {
		rwd_say(why, why_size, "plane %lu of a surface was asked for; a surface this driver imports has one plane or two",
		    plane);
		return NULL;
	}
	id = rwd_surface_id(iosurface);
	if (id == 0) {
		rwd_say(why, why_size, "that surface has no ID, so there is nothing for the "
		    "kernel to look up (a surface is given one when it is made shareable)");
		return NULL;
	}
	if (rwd_format_of_metal(want->pixel_format, &fourcc, &bytes_per_pixel, why,
	    why_size) != 0) {
		return NULL;
	}
	connection = rwd_shared_connection();
	if (connection == 0) {
		rwd_say(why, why_size, "no connection to RedwoodGPU: this kernel was booted "
		    "without gpu=1, so a surface's memory cannot be given to the GPU");
		return NULL;
	}
	/*
	 * Describe before taking. Describing allocates nothing, so every disagreement below is
	 * found without a buffer having to be given back -- and the disagreements are the
	 * point: the descriptor was built by CoreAnimation from the layer's drawableSize and
	 * pixelFormat, and the surface was made by CoreAnimation from the same two, but this
	 * driver has seen neither of those steps and must not assume they agreed.
	 */
	if (rwd_surface_describe(connection, id, &surface, why, why_size) != 0) {
		return NULL;
	}
	/*
	 * y2s41: ONE PLANE of a two-plane 4:2:0 surface as a texture of its own -- plane 0 as one byte a texel (R8Unorm, R8Uint),
	 * plane 1 as two (RG8Unorm, RG8Uint), each at its own size, offset and row length. This is how VideoToolbox's Metal
	 * transfer and CoreAnimation's per-plane path address such a surface (the whole surface as ONE texture is format 500,
	 * below). Laid over the plane in place, for reading and for writing; what cannot be (rwd_plane_fits_gpu) is refused by
	 * name.
	 */
	if (surface.planes == 2 && want->pixel_format != 500 && want->pixel_format != 540) {
		if (surface.pixelFormat != '420f' && surface.pixelFormat != '420v') {
			rwd_say(why, why_size, "surface %u is two planes of '%c%c%c%c', and only '420f' and '420v' are imported a "
			    "plane at a time", id, (char)(surface.pixelFormat >> 24), (char)(surface.pixelFormat >> 16),
			    (char)(surface.pixelFormat >> 8), (char)surface.pixelFormat);
			return NULL;
		}
		/* (y2s43: a texel of the plane's own element, or -- a plain format only -- of a whole number of them) */
		if (bytes_per_pixel != (plane == 0 ? 1u : 2u) &&
		    !(rwd_plain_format_size(want->pixel_format) != 0 && bytes_per_pixel > (plane == 0 ? 1u : 2u) &&
		      bytes_per_pixel % (plane == 0 ? 1u : 2u) == 0)) {
			rwd_say(why, why_size, "plane %lu of surface %u ('420f'/'420v') holds %u byte%s a texel and Metal format %u "
			    "holds %u", plane, id, plane == 0 ? 1u : 2u, plane == 0 ? "" : "s", want->pixel_format, bytes_per_pixel);
			return NULL;
		}
		if (rwd_plane_fits_gpu(&surface, (unsigned)plane, bytes_per_pixel, want->width, want->height, why, why_size) != 0) {
			/* y2s53: a plane the GPU cannot address in place only for where it starts or how long its rows are, and that
			 * holds the texture asked for: the caller is told what the surface is, and may serve the plane with a copy */
			if (took != NULL && (surface.planeRowBytes[plane] % 64 != 0 || surface.planeOffset[plane] % 64 != 0) &&
			    surface.planeRowBytes[plane] >= (uint64_t)want->width * bytes_per_pixel) {
				*took = surface;
			}
			return NULL;
		}
		import = rwd_import();
		if (import == NULL) {
			rwd_say(why, why_size, "plane %lu of surface %u fits, and the compiler library has no rwa_texture_from_surface",
			    plane, id);
			return NULL;
		}
		texture = import(mesa_device, want, (int)id, surface.planeOffset[plane], surface.planeRowBytes[plane], why,
		    why_size);
		if (texture != NULL) {
			static unsigned said;

			if (__sync_fetch_and_add(&said, 1) < 8) {
				m2m_log('I', "SURF", "plane %lu of two-plane surface %u imported in place as Metal format %u: %ux%u, %u "
				    "bytes a row from offset %u (usage 0x%x)", plane, id, want->pixel_format, want->width, want->height,
				    surface.planeRowBytes[plane], surface.planeOffset[plane], want->usage);
			}
			if (took != NULL) {
				*took = surface;
			}
		}
		return texture;
	}
	if (plane != 0) {
		rwd_say(why, why_size, "plane %lu of surface %u was asked for, and it has %u", plane, id,
		    surface.planes > 1 ? surface.planes : 1u);
		return NULL;
	}
	/*
	 * y2s43, y2s48, y2s49: A PLAIN FORMAT OVER A PLAIN ONE-PLANE SURFACE WHOSE ELEMENT IS ANOTHER SIZE (rwd_plain_over_surface says
	 * when): the texture is the leading width x texel bytes of each of the surface's rows, at the surface's own row length. The
	 * surface is then described to the checks below at the texture's width.
	 */
	{
		unsigned elem = surface.planes <= 1 ? rwd_plain_fourcc_size(surface.pixelFormat) : 0;
		unsigned texel = rwd_plain_format_size(want->pixel_format);

		if (rwd_plain_over_surface(elem, texel, surface.width, surface.height, surface.rowBytes, want->width, want->height) &&
		    !(texel == elem && surface.width == want->width)) {
			static unsigned said;

			if (__sync_fetch_and_add(&said, 1) < 12) {
				m2m_log('I', "SURF", "surface %u (%ux%u '%c%c%c%c', %u-byte elements, %u bytes a row) is taken as %ux%u texels "
				    "of %u byte%s for Metal format %u: the first %u bytes of each row", id, surface.width, surface.height,
				    (char)(surface.pixelFormat >> 24), (char)(surface.pixelFormat >> 16), (char)(surface.pixelFormat >> 8),
				    (char)surface.pixelFormat, elem, surface.rowBytes, want->width, want->height, texel, texel == 1 ? "" : "s",
				    want->pixel_format, want->width * texel);
			}
			surface.width = want->width;
			fourcc = surface.pixelFormat;       /* (served: the sizes were just compared) */
		}
	}
	/*
	 * y2s99a (subrect): a texture SMALLER than its one-plane surface is the surface's top-left corner at the surface's own row
	 * length -- what Metal allows (the texture's width and height at most the surface's) and what CoreMedia asks for in
	 * mediaserverd ("surface N is 64x64 and the descriptor asks for 52x52", 288 refusals in the logs). The surface is
	 * described to the checks below at the texture's size; the import lays the texture at the surface's offset and rows.
	 */
	if ((surface.width != want->width || surface.height != want->height) && rw_api99a_on(1 /* RW_A_SUBRECT */)) {
		unsigned bpp = rwd_plain_format_size(want->pixel_format);

		if (bpp == 0) {
			bpp = bytes_per_pixel;
		}
		if (rw_subrect_ok(surface.width, surface.height, surface.rowBytes, surface.planes, want->width, want->height, bpp)) {
			char what[96];
			static unsigned said;

			snprintf(what, sizeof(what), "%ux%u of %ux%u", want->width, want->height, surface.width, surface.height);
			rw_api99a_count(1, 1, what);
			if (__sync_fetch_and_add(&said, 1) < 8) {
				m2m_log('I', "SURF", "subrect: a %ux%u texture of Metal format %u over the top-left corner of surface %u (%ux%u, "
				    "%u bytes a row)", want->width, want->height, want->pixel_format, id, surface.width, surface.height,
				    surface.rowBytes);
			}
			surface.width = want->width;
			surface.height = want->height;
		} else {
			rw_api99a_count(1, 0, "not a sub-rectangle");
		}
	}
	if (surface.width != want->width || surface.height != want->height) {
		rwd_say(why, why_size, "surface %u is %ux%u and the descriptor asks for %ux%u",
		    id, surface.width, surface.height, want->width, want->height);
		return NULL;
	}
	if (fourcc == 0 && !rwd_fourcc_serves(want->pixel_format, surface.pixelFormat)) {
		/* y2s42: a plain format (no FourCC of its own) over a surface that is not a plain array of elements its size */
		rwd_say(why, why_size, "surface %u is '%c%c%c%c' and Metal format %u needs a single plane of %u-byte elements", id,
		    (char)(surface.pixelFormat >> 24), (char)(surface.pixelFormat >> 16), (char)(surface.pixelFormat >> 8),
		    (char)surface.pixelFormat, want->pixel_format, bytes_per_pixel);
		return NULL;
	}
	if (surface.pixelFormat != fourcc && !rwd_fourcc_serves(want->pixel_format, surface.pixelFormat)) {
		rwd_say(why, why_size, "surface %u is '%c%c%c%c' and Metal format %u is "
		    "'%c%c%c%c'", id, (char)(surface.pixelFormat >> 24),
		    (char)(surface.pixelFormat >> 16), (char)(surface.pixelFormat >> 8),
		    (char)surface.pixelFormat, want->pixel_format, (char)(fourcc >> 24),
		    (char)(fourcc >> 16), (char)(fourcc >> 8), (char)fourcc);
		return NULL;
	}
	if (rwd_surface_fits_adreno(&surface, bytes_per_pixel, why, why_size) != 0) {
		/* a row length the GPU cannot address in place, and nothing else wrong: the caller
		 * can serve the surface with a copy (newTextureWithDescriptor:iosurface:plane:), so
		 * it is told what the surface is */
		if (took != NULL && surface.planes <= 1 && surface.rowBytes % 64 != 0 &&
		    surface.rowBytes >= (uint64_t)surface.width * bytes_per_pixel) {
			*took = surface;
		}
		return NULL;
	}
	/*
	 * Everything that could be checked has been. The one thing left is the call that does
	 * not exist, and it is refused here with the whole of what was learned, because this
	 * sentence is what the next boot will have to go on.
	 */
	import = rwd_import();
	if (import == NULL) {
		rwd_say(why, why_size, "surface %u fits (%ux%u '%c%c%c%c', %u bytes a row, "
		    "%llu bytes from offset %u) and the kernel would hand its memory over as a "
		    "GPU buffer, but the compiler library in this image has no "
		    "rwa_texture_from_surface: that image was built before the call existed, and "
		    "without it there is no drawable", id, surface.width, surface.height,
		    (char)(surface.pixelFormat >> 24), (char)(surface.pixelFormat >> 16),
		    (char)(surface.pixelFormat >> 8), (char)surface.pixelFormat,
		    surface.rowBytes, (unsigned long long)surface.size, surface.offset);
		return NULL;
	}
	/*
	 * Not taken here: `import` takes it, on freedreno's own connection and into
	 * freedreno's own table of buffers, and rwa_texture_destroy gives it back. Taking it
	 * here as well would make a second buffer over the same memory -- the kernel makes one
	 * per user client -- and then there would be two owners of one surface and only one
	 * release.
	 */
	if (surface.planes == 2) {
		if (rwd_import_planes == NULL) {
			rwd_say(why, why_size, "surface %u is two planes and the compiler library has no "
			    "rwa_texture_from_surface_planes", id);
			return NULL;
		}
		texture = rwd_import_planes(mesa_device, want, (int)id, surface.planeOffset[0],
		    surface.planeRowBytes[0], surface.planeOffset[1], surface.planeRowBytes[1], why,
		    why_size);
	} else {
		texture = import(mesa_device, want, (int)id, surface.offset, surface.rowBytes, why,
		    why_size);
	}
	if (texture == NULL) {
		return NULL;    /* the library wrote the sentence; it names the GPU's own reason */
	}
	if (surface.planes == 2) {
		/* y2s36: the first few two-plane imports that worked, with what they were laid over */
		static unsigned said;

		if (__sync_fetch_and_add(&said, 1) < 8) {
			m2m_log('I', "SURF", "two-plane surface %u imported in place: %ux%u '%c%c%c%c' as Metal "
			    "format %u, luma %u bytes a row from offset %u, chroma %u bytes a row from offset %u, "
			    "%llu bytes (the surface's own row length %u is not used)", id, surface.width,
			    surface.height, (char)(surface.pixelFormat >> 24), (char)(surface.pixelFormat >> 16),
			    (char)(surface.pixelFormat >> 8), (char)surface.pixelFormat, want->pixel_format,
			    surface.planeRowBytes[0], surface.planeOffset[0], surface.planeRowBytes[1],
			    surface.planeOffset[1], (unsigned long long)surface.size, surface.rowBytes);
		}
	}
	if (took != NULL) {
		*took = surface;    /* what the surface turned out to be, for whoever logs it */
	}
	return texture;
}
