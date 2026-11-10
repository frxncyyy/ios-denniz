/* SPDX-License-Identifier: MIT */
/*
 * fake_objc.c -- a made-up Objective-C runtime and AVFoundation for the host oracle of probe_av.c. HOST TESTS ONLY.
 *
 * What it is: every message probe_av.c sends is taken by an x86_64 assembly trampoline (objc_msgSend /
 * objc_msgSend_stret), which saves the argument registers and the caller's stack pointer and calls a C dispatcher.
 * The dispatcher records (receiver class, selector, class-vs-instance), then returns a scripted object or value.
 * This is necessary because the probe's calls use the real ABIs: a 24-byte CMTime is passed on the stack to
 * -appendPixelBuffer:withPresentationTime:, CMTime is returned through a hidden pointer (-duration) and CGSize in
 * xmm0/xmm1 (-naturalSize). A plain variadic C fake cannot do that.
 *
 * What it is NOT: a test of AVFoundation. It can prove ordering, nil handling, dictionary contents and the block /
 * delegate plumbing of probe_av.c; it cannot prove that the selectors, constants or settings keys are accepted by
 * the phone's AVFoundation. STATUS.json says so.
 *
 * The objects live in static arrays (no allocation), so ASan/LSan see nothing to free; a reset between tests starts
 * the arena again. The assembly is x86_64 SysV only -- the host this runs on.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "n2_apple.h"
#include "probe_objc.h"
#include "fake_objc.h"

#define FOBJ_MAGIC 0x0b1ec7u
#define FCLS_MAGIC 0xc1a55u
#define FPIX_MAGIC 0x91b1e5u
#define FMAX_OBJECTS 768
#define FMAX_RECORDS 12000
#define FMAX_CLASS_REQUESTS 2048
#define FMAX_SYMBOLS 256
#define FMAX_FRAMEWORKS 8
#define FMAX_PIXEL_BUFFERS 8

enum fake_kind {
	FK_PLAIN, FK_STRING, FK_URL, FK_NUMBER, FK_DICT, FK_ERROR, FK_WRITER, FK_INPUT, FK_ADAPTOR,
	FK_ASSET, FK_TRACK, FK_READER, FK_OUTPUT, FK_SESSION, FK_DEVICE, FK_DEVINPUT, FK_MOVIE,
	FK_ARRAY, FK_SBUF, FK_POOL, FK_DELEGATE
};

struct fake_object {
	uint32_t magic;
	int      kind;
	char     cls[48];
	char     text[512];
	long     number;
	double   d0, d1;
	void    *p0, *p1, *p2;
	int      i0, i1;
	id       dict_keys[16];
	id       dict_values[16];
	int      dict_count;
	id       array[16];
	int      array_count;
	id       image;
	long     samples;
};

struct fake_class {
	uint32_t magic;
	char     name[64];
	int      dynamic;
	int      registered;
	struct {
		char selector[96];
		IMP  imp;
	} methods[4];
	int method_count;
};

struct fake_pixel_buffer {
	uint32_t magic;
	int      in_use;
	size_t   width, height;
	size_t   stride0, stride1;
	uint32_t format;
	uint8_t *data;
	size_t   capacity;
};

struct fake_block_buffer {
	uint32_t magic;
	const void *data;
	size_t length;
};

struct fake_record {
	char cls[48];
	char selector[96];
	int  is_class;
};

struct fake_constant {
	const char *name;
	const char *value;
	id object;
};

extern void *fake_regs[8];
extern double fake_ret_d0, fake_ret_d1;

/* ---- the scripted state ------------------------------------------------------------------------------------ */

struct fake_script fake_script = {
	.start_writing = 1,
	.writer_status = 2,
	.append_result = 1,
	.finish_calls_block = 1,
	.capture_authorization = 3,
	.capture_success = 1,
	.reader_frames = 60,
	.reader_audio_samples = 88200,
	.reader_status = 2,
};
struct fake_stats fake_stats;

static struct fake_object objects[FMAX_OBJECTS];
static int object_count;
static struct fake_record records[FMAX_RECORDS];
static int record_count;
static char class_requests[FMAX_CLASS_REQUESTS][64];
static int class_request_count;
static char symbol_requests[FMAX_SYMBOLS][96];
static int symbol_request_count;
static char frameworks[FMAX_FRAMEWORKS][128];
static int framework_count;
static char writer_url_text[512];
static char writer_file_type_text[512];
static struct fake_pixel_buffer *pixel_buffers[FMAX_PIXEL_BUFFERS];
static struct fake_block_buffer block_buffers[256];
static int block_buffer_count;

#define FCLASS(n) { .magic = FCLS_MAGIC, .name = n, .registered = 1 }
static struct fake_class static_classes[] = {
	FCLASS("NSObject"),
	FCLASS("NSString"),
	FCLASS("NSURL"),
	FCLASS("NSNumber"),
	FCLASS("NSDictionary"),
	FCLASS("NSArray"),
	FCLASS("NSError"),
	FCLASS("AVAssetWriter"),
	FCLASS("AVAssetWriterInput"),
	FCLASS("AVAssetWriterInputPixelBufferAdaptor"),
	FCLASS("AVURLAsset"),
	FCLASS("AVAssetTrack"),
	FCLASS("AVAssetReader"),
	FCLASS("AVAssetReaderTrackOutput"),
	FCLASS("AVCaptureDevice"),
	FCLASS("AVCaptureDeviceInput"),
	FCLASS("AVCaptureSession"),
	FCLASS("AVCaptureMovieFileOutput"),
};
static struct fake_class dynamic_class = { .magic = FCLS_MAGIC, .name = "N2ProbeMovieDelegate", .dynamic = 1 };
#undef FCLASS

static struct fake_constant constants[] = {
	{ "AVFileTypeMPEG4", "public.mpeg-4", NULL },
	{ "AVFileTypeQuickTimeMovie", "com.apple.quicktime-movie", NULL },
	{ "AVMediaTypeVideo", "vide", NULL },
	{ "AVMediaTypeAudio", "soun", NULL },
	{ "AVVideoCodecKey", "AVVideoCodecKey", NULL },
	{ "AVVideoWidthKey", "AVVideoWidthKey", NULL },
	{ "AVVideoHeightKey", "AVVideoHeightKey", NULL },
	{ "AVVideoCompressionPropertiesKey", "AVVideoCompressionPropertiesKey", NULL },
	{ "AVVideoAverageBitRateKey", "AVVideoAverageBitRateKey", NULL },
	{ "AVFormatIDKey", "AVFormatIDKey", NULL },
	{ "AVSampleRateKey", "AVSampleRateKey", NULL },
	{ "AVNumberOfChannelsKey", "AVNumberOfChannelsKey", NULL },
	{ "AVEncoderBitRateKey", "AVEncoderBitRateKey", NULL },
	{ "AVLinearPCMBitDepthKey", "AVLinearPCMBitDepthKey", NULL },
	{ "AVLinearPCMIsFloatKey", "AVLinearPCMIsFloatKey", NULL },
	{ "AVLinearPCMIsBigEndianKey", "AVLinearPCMIsBigEndianKey", NULL },
	{ "AVLinearPCMIsNonInterleaved", "AVLinearPCMIsNonInterleaved", NULL },
	{ "AVCaptureSessionPresetHigh", "AVCaptureSessionPresetHigh", NULL },
	{ "AVCaptureSessionPreset1280x720", "AVCaptureSessionPreset1280x720", NULL },
	{ "kCVPixelBufferPixelFormatTypeKey", "PixelFormatType", NULL },
	{ "kCVPixelBufferWidthKey", "Width", NULL },
	{ "kCVPixelBufferHeightKey", "Height", NULL },
};

/* ---- objects ----------------------------------------------------------------------------------------------- */

static struct fake_object *
new_object(const char *class_name, int kind)
{
	struct fake_object *object;

	if (object_count >= FMAX_OBJECTS) {
		fprintf(stderr, "fake_objc: object arena exhausted\n");
		abort();
	}
	object = &objects[object_count++];
	memset(object, 0, sizeof(*object));
	object->magic = FOBJ_MAGIC;
	object->kind = kind;
	snprintf(object->cls, sizeof(object->cls), "%s", class_name);
	return object;
}

static int
kind_for_class(const char *class_name)
{
	if (strcmp(class_name, "AVCaptureSession") == 0) {
		return FK_SESSION;
	}
	if (strcmp(class_name, "AVCaptureMovieFileOutput") == 0) {
		return FK_MOVIE;
	}
	if (strcmp(class_name, "AVAssetReaderTrackOutput") == 0) {
		return FK_OUTPUT;
	}
	if (strcmp(class_name, "N2ProbeMovieDelegate") == 0) {
		return FK_DELEGATE;
	}
	return FK_PLAIN;
}

static struct fake_object *
object_of(id value)
{
	struct fake_object *object = (struct fake_object *)value;

	if (object == NULL || object->magic != FOBJ_MAGIC) {
		return NULL;
	}
	return object;
}

static id
make_string(const char *text)
{
	struct fake_object *object = new_object("NSString", FK_STRING);

	snprintf(object->text, sizeof(object->text), "%s", text != NULL ? text : "");
	return (id)object;
}

static id
make_error(const char *text)
{
	struct fake_object *object = new_object("NSError", FK_ERROR);

	snprintf(object->text, sizeof(object->text), "%s", text != NULL ? text : "");
	return (id)object;
}

static id
make_array(void)
{
	struct fake_object *object = new_object("NSArray", FK_ARRAY);

	return (id)object;
}

static void
array_add(id array, id item)
{
	struct fake_object *object = object_of(array);

	if (object != NULL && object->array_count < 16) {
		object->array[object->array_count++] = item;
	}
}

static id
make_dictionary(id *objects_in, id *keys_in, unsigned long count)
{
	struct fake_object *object = new_object("NSDictionary", FK_DICT);

	if (count > 16) {
		count = 16;
	}
	for (unsigned long i = 0; i < count; i++) {
		object->dict_keys[i] = keys_in[i];
		object->dict_values[i] = objects_in[i];
	}
	object->dict_count = (int)count;
	return (id)object;
}

static id
asset_track(id asset, const char *media_type)
{
	struct fake_object *object = object_of(asset);
	int slot = strcmp(media_type, "soun") == 0 ? 1 : 0;
	struct fake_object *track;

	if (object == NULL) {
		return NULL;
	}
	if (object->array[slot] == NULL) {
		track = new_object("AVAssetTrack", FK_TRACK);
		snprintf(track->text, sizeof(track->text), "%s", media_type);
		object->array[slot] = (id)track;
		if (object->array_count < slot + 1) {
			object->array_count = slot + 1;
		}
	}
	return object->array[slot];
}

static void
set_size_result(double width, double height)
{
	extern double fake_ret_d0, fake_ret_d1;

	fake_ret_d0 = width;
	fake_ret_d1 = height;
}

static void
set_float_result(float value)
{
	extern double fake_ret_d0;
	uint32_t bits;
	uint64_t wide = 0;

	memcpy(&bits, &value, sizeof(bits));
	wide = bits;
	memcpy(&fake_ret_d0, &wide, sizeof(wide));
}

/* ---- pixel buffers (CoreVideo) ----------------------------------------------------------------------------- */

static struct fake_pixel_buffer *
pixel_buffer_new(size_t width, size_t height, uint32_t format)
{
	struct fake_pixel_buffer *buffer = NULL;
	size_t stride = (width + 63u) & ~(size_t)63u;
	size_t needed = stride * height + stride * ((height + 1u) / 2u);

	for (int i = 0; i < FMAX_PIXEL_BUFFERS; i++) {
		if (pixel_buffers[i] != NULL && !pixel_buffers[i]->in_use) {
			buffer = pixel_buffers[i];
			break;
		}
	}
	if (buffer == NULL) {
		for (int i = 0; i < FMAX_PIXEL_BUFFERS; i++) {
			if (pixel_buffers[i] == NULL) {
				buffer = calloc(1, sizeof(*buffer));
				if (buffer == NULL) {
					return NULL;
				}
				buffer->magic = FPIX_MAGIC;
				pixel_buffers[i] = buffer;
				break;
			}
		}
	}
	if (buffer == NULL) {
		return NULL;
	}
	if (buffer->capacity < needed) {
		uint8_t *data = realloc(buffer->data, needed);

		if (data == NULL) {
			return NULL;
		}
		buffer->data = data;
		buffer->capacity = needed;
	}
	memset(buffer->data, 128, stride * height);
	memset(buffer->data + stride * height, 0, needed - stride * height);
	buffer->in_use = 1;
	buffer->width = width;
	buffer->height = height;
	buffer->stride0 = stride;
	buffer->stride1 = stride;
	buffer->format = format;
	return buffer;
}

int32_t
CVPixelBufferCreate(CFAllocatorRef allocator, size_t width, size_t height, uint32_t pixelFormat,
                    CFDictionaryRef attributes, CVPixelBufferRef *pixelBufferOut)
{
	struct fake_pixel_buffer *buffer;

	(void)allocator;
	(void)attributes;
	if (pixelBufferOut == NULL) {
		return -6660;
	}
	buffer = pixel_buffer_new(width, height, pixelFormat);
	if (buffer == NULL) {
		*pixelBufferOut = NULL;
		return -6661;
	}
	*pixelBufferOut = (CVPixelBufferRef)buffer;
	return 0;
}

int32_t
CVPixelBufferPoolCreatePixelBuffer(CFAllocatorRef allocator, CVPixelBufferPoolRef pool, CVPixelBufferRef *pixelBufferOut)
{
	struct fake_object *object = object_of((id)pool);
	struct fake_object *adaptor = object != NULL ? object_of(object->p0) : NULL;
	size_t width = 640, height = 480;

	(void)allocator;
	if (adaptor != NULL) {
		/* the adaptor carries no dimensions in this fake; the probe asks for 640x480 */
		(void)adaptor;
	}
	return CVPixelBufferCreate(NULL, width, height, N2_PIXFMT_420V, NULL, pixelBufferOut);
}

void
CVPixelBufferRelease(CVPixelBufferRef pixelBuffer)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	if (buffer != NULL && buffer->magic == FPIX_MAGIC) {
		buffer->in_use = 0;
	}
}

int32_t
CVPixelBufferLockBaseAddress(CVPixelBufferRef pixelBuffer, uint64_t flags)
{
	(void)pixelBuffer;
	(void)flags;
	return 0;
}

int32_t
CVPixelBufferUnlockBaseAddress(CVPixelBufferRef pixelBuffer, uint64_t flags)
{
	(void)pixelBuffer;
	(void)flags;
	return 0;
}

void *
CVPixelBufferGetBaseAddressOfPlane(CVPixelBufferRef pixelBuffer, size_t plane)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	if (buffer == NULL || buffer->magic != FPIX_MAGIC || buffer->data == NULL) {
		return NULL;
	}
	return plane == 0 ? (void *)buffer->data : (void *)(buffer->data + buffer->stride0 * buffer->height);
}

size_t
CVPixelBufferGetBytesPerRowOfPlane(CVPixelBufferRef pixelBuffer, size_t plane)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	(void)plane;
	return buffer != NULL && buffer->magic == FPIX_MAGIC ? buffer->stride0 : 0;
}

size_t
CVPixelBufferGetWidth(CVPixelBufferRef pixelBuffer)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	return buffer != NULL && buffer->magic == FPIX_MAGIC ? buffer->width : 0;
}

size_t
CVPixelBufferGetHeight(CVPixelBufferRef pixelBuffer)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	return buffer != NULL && buffer->magic == FPIX_MAGIC ? buffer->height : 0;
}

size_t
CVPixelBufferGetPlaneCount(CVPixelBufferRef pixelBuffer)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	return buffer != NULL && buffer->magic == FPIX_MAGIC ? 2 : 0;
}

uint32_t
CVPixelBufferGetPixelFormatType(CVPixelBufferRef pixelBuffer)
{
	struct fake_pixel_buffer *buffer = (struct fake_pixel_buffer *)pixelBuffer;

	return buffer != NULL && buffer->magic == FPIX_MAGIC ? buffer->format : 0;
}

/* ---- CoreMedia sample buffers (only what the probe asks) --------------------------------------------------- */

OSStatus
CMBlockBufferCreateWithMemoryBlock(CFAllocatorRef structureAllocator, void *memoryBlock, size_t blockLength,
                                   CFAllocatorRef blockAllocator, const void *customBlockSource, size_t offsetToData,
                                   size_t dataLength, uint32_t flags, CMBlockBufferRef *blockBufferOut)
{
	static struct fake_block_buffer fallback;
	struct fake_block_buffer *block;

	(void)structureAllocator;
	(void)blockAllocator;
	(void)customBlockSource;
	(void)offsetToData;
	(void)flags;
	if (block_buffer_count < (int)(sizeof(block_buffers) / sizeof(block_buffers[0]))) {
		block = &block_buffers[block_buffer_count++];
	} else {
		block = &fallback;
	}
	block->magic = FOBJ_MAGIC;
	block->data = memoryBlock;
	block->length = dataLength != 0 ? dataLength : blockLength;
	*blockBufferOut = (CMBlockBufferRef)block;
	return 0;
}

OSStatus
CMAudioFormatDescriptionCreate(CFAllocatorRef allocator, const struct n2o_asbd *asbd, size_t layoutSize,
                               const void *layout, size_t magicCookieSize, const void *magicCookie,
                               CFDictionaryRef extensions, CMAudioFormatDescriptionRef *formatDescriptionOut)
{
	(void)allocator;
	(void)asbd;
	(void)layoutSize;
	(void)layout;
	(void)magicCookieSize;
	(void)magicCookie;
	(void)extensions;
	*formatDescriptionOut = (CMAudioFormatDescriptionRef)(uintptr_t)0x1;
	return 0;
}

OSStatus
CMSampleBufferCreateReady(CFAllocatorRef allocator, CMBlockBufferRef dataBuffer, CMFormatDescriptionRef format,
                          CMItemCount numSamples, CMItemCount numSampleTimingEntries,
                          const CMSampleTimingInfo *sampleTimingArray, CMItemCount numSampleSizeEntries,
                          const size_t *sampleSizeArray, CMSampleBufferRef *sampleBufferOut)
{
	struct fake_object *sample = new_object("CMSampleBuffer", FK_SBUF);

	(void)allocator;
	(void)dataBuffer;
	(void)format;
	(void)numSampleTimingEntries;
	(void)sampleTimingArray;
	(void)numSampleSizeEntries;
	(void)sampleSizeArray;
	sample->samples = (long)numSamples;
	*sampleBufferOut = (CMSampleBufferRef)sample;
	return 0;
}

CMItemCount
CMSampleBufferGetNumSamples(CMSampleBufferRef sampleBuffer)
{
	struct fake_object *sample = object_of((id)sampleBuffer);

	return sample != NULL ? sample->samples : 0;
}

CVImageBufferRef
CMSampleBufferGetImageBuffer(CMSampleBufferRef sampleBuffer)
{
	struct fake_object *sample = object_of((id)sampleBuffer);

	return sample != NULL ? (CVImageBufferRef)sample->image : NULL;
}

void
CFRelease(CFTypeRef object)
{
	(void)object;
}

/* ---- the block call ---------------------------------------------------------------------------------------- */

static void
call_block(id block)
{
	struct n2o_block *literal = (struct n2o_block *)block;

	if (literal != NULL && literal->invoke != NULL) {
		((void (*)(void *))(void *)literal->invoke)(literal);
	}
}

/* ---- dispatch ---------------------------------------------------------------------------------------------- */

static void
record_message(const char *class_name, const char *selector, int is_class)
{
	if (record_count >= FMAX_RECORDS) {
		return;
	}
	snprintf(records[record_count].cls, sizeof(records[record_count].cls), "%s", class_name);
	snprintf(records[record_count].selector, sizeof(records[record_count].selector), "%s", selector);
	records[record_count].is_class = is_class;
	record_count++;
}

static int
selector_is(const char *selector, const char *name)
{
	return strcmp(selector, name) == 0;
}

static id
lookup_class(const char *name)
{
	for (unsigned i = 0; i < sizeof(static_classes) / sizeof(static_classes[0]); i++) {
		if (strcmp(static_classes[i].name, name) == 0) {
			return (id)&static_classes[i];
		}
	}
	if (dynamic_class.registered && strcmp(dynamic_class.name, name) == 0) {
		return (id)&dynamic_class;
	}
	return NULL;
}

Class
objc_getClass(const char *name)
{
	Class cls;

	if (class_request_count < FMAX_CLASS_REQUESTS) {
		snprintf(class_requests[class_request_count++], sizeof(class_requests[0]), "%s", name);
	}
	cls = (Class)lookup_class(name);
	return cls;
}

SEL
sel_registerName(const char *name)
{
	return (SEL)(uintptr_t)name;
}

Class
objc_allocateClassPair(Class superclass, const char *name, size_t extra_bytes)
{
	(void)superclass;
	(void)extra_bytes;
	if (strcmp(name, dynamic_class.name) != 0 || dynamic_class.registered) {
		return NULL;
	}
	dynamic_class.method_count = 0;
	return (Class)&dynamic_class;
}

void
objc_registerClassPair(Class cls)
{
	struct fake_class *fake = (struct fake_class *)cls;

	if (fake != NULL && fake->magic == FCLS_MAGIC) {
		fake->registered = 1;
	}
}

BOOL
class_addMethod(Class cls, SEL name, IMP imp, const char *types)
{
	struct fake_class *fake = (struct fake_class *)cls;

	(void)types;
	if (fake == NULL || fake->magic != FCLS_MAGIC || fake->method_count >= 4) {
		return 0;
	}
	snprintf(fake->methods[fake->method_count].selector, sizeof(fake->methods[0].selector), "%s",
	         (const char *)(uintptr_t)name);
	fake->methods[fake->method_count].imp = imp;
	fake->method_count++;
	return 1;
}


static void *
dispatch_message(id self, const char *selector, void *a1, void *a2, void *a3, void *a4, void *stack)
{
	struct fake_object *object = object_of(self);
	struct fake_class *class_object = (struct fake_class *)self;
	const char *class_name;
	int is_class;

	if (self == NULL) {
		record_message("(nil)", selector, 0);
		return NULL;
	}
	if (class_object != NULL && class_object->magic == FCLS_MAGIC) {
		class_name = class_object->name;
		is_class = 1;
		object = NULL;
	} else if (object != NULL) {
		class_name = object->cls;
		is_class = 0;
	} else {
		record_message("(foreign)", selector, 0);
		return NULL;
	}
	record_message(class_name, selector, is_class);

	/* messages that do not depend on the class of the receiver */
	if (!is_class) {
		if (selector_is(selector, "init") || selector_is(selector, "self") || selector_is(selector, "retain")) {
			return self;
		}
		if (selector_is(selector, "release")) {
			return NULL;
		}
	}

	if (is_class) {
		if (selector_is(selector, "alloc")) {
			return (void *)new_object(class_name, kind_for_class(class_name));
		}
		if (strcmp(class_name, "NSString") == 0 && selector_is(selector, "stringWithUTF8String:")) {
			return (void *)make_string((const char *)a1);
		}
		if (strcmp(class_name, "NSURL") == 0 && selector_is(selector, "fileURLWithPath:")) {
			struct fake_object *url = new_object("NSURL", FK_URL);

			snprintf(url->text, sizeof(url->text), "%s", fake_text((id)a1));
			return (void *)url;
		}
		if (strcmp(class_name, "NSNumber") == 0 && selector_is(selector, "numberWithInt:")) {
			struct fake_object *number = new_object("NSNumber", FK_NUMBER);

			number->number = (int)(intptr_t)a1;
			return (void *)number;
		}
		if (strcmp(class_name, "NSNumber") == 0 && selector_is(selector, "numberWithBool:")) {
			struct fake_object *number = new_object("NSNumber", FK_NUMBER);

			number->number = (int)(intptr_t)a1 != 0;
			return (void *)number;
		}
		if (strcmp(class_name, "NSDictionary") == 0 && selector_is(selector, "dictionaryWithObjects:forKeys:count:")) {
			return (void *)make_dictionary((id *)a1, (id *)a2, (unsigned long)(uintptr_t)a3);
		}
		if (strcmp(class_name, "AVAssetWriter") == 0 && selector_is(selector, "assetWriterWithURL:fileType:error:")) {
			struct fake_object *writer = new_object("AVAssetWriter", FK_WRITER);

			writer->p0 = a1;
			writer->p1 = a2;
			snprintf(writer_url_text, sizeof(writer_url_text), "%s", fake_text((id)a1));
			snprintf(writer_file_type_text, sizeof(writer_file_type_text), "%s", fake_text((id)a2));
			return (void *)writer;
		}
		if (strcmp(class_name, "AVAssetWriterInput") == 0 &&
		    selector_is(selector, "assetWriterInputWithMediaType:outputSettings:")) {
			struct fake_object *input = new_object("AVAssetWriterInput", FK_INPUT);

			snprintf(input->text, sizeof(input->text), "%s", fake_text((id)a1));
			input->p0 = a2;
			return (void *)input;
		}
		if (strcmp(class_name, "AVAssetWriterInputPixelBufferAdaptor") == 0 &&
		    selector_is(selector, "assetWriterInputPixelBufferAdaptorWithAssetWriterInput:sourcePixelBufferAttributes:")) {
			struct fake_object *adaptor = new_object("AVAssetWriterInputPixelBufferAdaptor", FK_ADAPTOR);
			struct fake_object *pool = new_object("CVPixelBufferPool", FK_POOL);

			adaptor->p0 = a1;
			adaptor->p1 = a2;
			pool->p0 = (void *)adaptor;
			adaptor->p2 = (void *)pool;
			return (void *)adaptor;
		}
		if (strcmp(class_name, "AVURLAsset") == 0 && selector_is(selector, "assetWithURL:")) {
			struct fake_object *asset = new_object("AVURLAsset", FK_ASSET);

			asset->p0 = a1;
			return (void *)asset;
		}
		if (strcmp(class_name, "AVAssetReader") == 0 && selector_is(selector, "assetReaderWithAsset:error:")) {
			struct fake_object *reader = new_object("AVAssetReader", FK_READER);

			reader->p0 = a1;
			return (void *)reader;
		}
		if (strcmp(class_name, "AVCaptureDevice") == 0 && selector_is(selector, "defaultDeviceWithMediaType:")) {
			struct fake_object *device = new_object("AVCaptureDevice", FK_DEVICE);

			snprintf(device->text, sizeof(device->text), "%s", fake_text((id)a1));
			return (void *)device;
		}
		if (strcmp(class_name, "AVCaptureDevice") == 0 && selector_is(selector, "authorizationStatusForMediaType:")) {
			return (void *)(intptr_t)fake_script.capture_authorization;
		}
		if (strcmp(class_name, "AVCaptureDeviceInput") == 0 && selector_is(selector, "deviceInputWithDevice:error:")) {
			struct fake_object *input = new_object("AVCaptureDeviceInput", FK_DEVINPUT);

			input->p0 = a1;
			return (void *)input;
		}
		return NULL;
	}

	/* the run-time built delegate class */
	if (object != NULL && object->kind == FK_DELEGATE) {
		for (int i = 0; i < dynamic_class.method_count; i++) {
			if (selector_is(selector, dynamic_class.methods[i].selector)) {
				((void (*)(id, SEL, id, id, id, id))(void *)dynamic_class.methods[i].imp)(
					self, (SEL)(uintptr_t)selector, (id)a1, (id)a2, (id)a3, (id)a4);
				return NULL;
			}
		}
	}

	if (object == NULL) {
		return NULL;
	}
	switch (object->kind) {
	case FK_STRING:
		if (selector_is(selector, "UTF8String")) {
			return (void *)object->text;
		}
		if (selector_is(selector, "description")) {
			return (void *)self;
		}
		break;
	case FK_URL:
		if (selector_is(selector, "path")) {
			return (void *)object->text;
		}
		if (selector_is(selector, "description")) {
			return (void *)self;
		}
		break;
	case FK_NUMBER:
		if (selector_is(selector, "intValue")) {
			return (void *)(intptr_t)object->number;
		}
		if (selector_is(selector, "description")) {
			return (void *)self;
		}
		break;
	case FK_ERROR:
		if (selector_is(selector, "description")) {
			return (void *)make_string(object->text);
		}
		break;
	case FK_DICT:
		break;
	case FK_ARRAY:
		if (selector_is(selector, "count")) {
			return (void *)(intptr_t)object->array_count;
		}
		if (selector_is(selector, "objectAtIndex:")) {
			unsigned long index = (unsigned long)(uintptr_t)a1;

			return index < (unsigned long)object->array_count ? (void *)object->array[index] : NULL;
		}
		break;
	case FK_WRITER:
		if (selector_is(selector, "canAddInput:")) {
			return (void *)1;
		}
		if (selector_is(selector, "addInput:")) {
			object->i0++;
			return NULL;
		}
		if (selector_is(selector, "startWriting")) {
			fake_stats.start_writing_calls++;
			if (!fake_script.start_writing) {
				object->p2 = (void *)make_error(fake_script.writer_error != NULL ? fake_script.writer_error :
				                               "scripted start failure");
				return NULL;
			}
			return (void *)1;
		}
		if (selector_is(selector, "status")) {
			return (void *)(intptr_t)fake_script.writer_status;
		}
		if (selector_is(selector, "error")) {
			return object->p2;
		}
		if (selector_is(selector, "finishWritingWithCompletionHandler:")) {
			char path[512];
			int fd;

			fake_stats.finish_calls++;
			/* an AVAssetWriter writes its file; the probe reports its size after this block returns */
			strncpy(path, fake_text((id)object->p0), sizeof(path) - 1);
			path[sizeof(path) - 1] = 0;
			fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
			if (fd >= 0) {
				char payload[4096];

				memset(payload, 'W', sizeof(payload));
				if (write(fd, payload, sizeof(payload)) < 0) {
					/* nothing to do in the fake */
				}
				close(fd);
			}
			if (fake_script.finish_calls_block) {
				call_block((id)a1);
			}
			return NULL;
		}
		break;
	case FK_INPUT:
		if (selector_is(selector, "setExpectsMediaDataInRealTime:")) {
			object->i0 = (int)(intptr_t)a1;
			return NULL;
		}
		if (selector_is(selector, "isReadyForMoreMediaData")) {
			return (void *)1;
		}
		if (selector_is(selector, "markAsFinished")) {
			fake_stats.mark_finished_calls++;
			return NULL;
		}
		if (selector_is(selector, "appendSampleBuffer:")) {
			fake_stats.append_audio_calls++;
			fake_stats.audio_samples += CMSampleBufferGetNumSamples((CMSampleBufferRef)a1);
			return (void *)(intptr_t)fake_script.append_result;
		}
		break;
	case FK_ADAPTOR:
		if (selector_is(selector, "pixelBufferPool")) {
			return object->p2;
		}
		if (selector_is(selector, "appendPixelBuffer:withPresentationTime:")) {
			const uint64_t *words = (const uint64_t *)stack;
			uint64_t timing_word;

			fake_stats.append_pixel_calls++;
			if (words != NULL) {
				timing_word = words[2];     /* words[0] return address, words[1] CMTime.value, words[2] timescale|flags */
				fake_stats.append_pixel_last_value = (long)words[1];
				fake_stats.append_pixel_last_timescale = (int)(timing_word & 0xffffffffu);
			}
			if (object->p0 != NULL) {
				object_of(object->p0);
			}
			return (void *)(intptr_t)fake_script.append_result;
		}
		break;
	case FK_ASSET:
		if (selector_is(selector, "tracks")) {
			id array = make_array();

			array_add(array, asset_track(self, "vide"));
			array_add(array, asset_track(self, "soun"));
			return (void *)array;
		}
		if (selector_is(selector, "tracksWithMediaType:")) {
			id array = make_array();

			array_add(array, asset_track(self, fake_text((id)a1)));
			return (void *)array;
		}
		break;
	case FK_TRACK:
		if (selector_is(selector, "naturalSize")) {
			set_size_result(640.0, 480.0);
			return NULL;
		}
		if (selector_is(selector, "nominalFrameRate")) {
			set_float_result(30.0f);
			return NULL;
		}
		break;
	case FK_READER:
		if (selector_is(selector, "canAddOutput:")) {
			return (void *)1;
		}
		if (selector_is(selector, "addOutput:")) {
			if (object->p1 == NULL) {
				object->p1 = a1;
			} else {
				object->p2 = a1;
			}
			return NULL;
		}
		if (selector_is(selector, "startReading")) {
			return (void *)1;
		}
		if (selector_is(selector, "status")) {
			return (void *)(intptr_t)fake_script.reader_status;
		}
		if (selector_is(selector, "error")) {
			return NULL;
		}
		break;
	case FK_OUTPUT:
		if (selector_is(selector, "initWithTrack:outputSettings:")) {
			object->p0 = a1;
			object->p1 = a2;
			object->i0 = strcmp(fake_text((id)a1), "soun") == 0 ? 1 : 0;
			return self;
		}
		if (selector_is(selector, "copyNextSampleBuffer")) {
			if (object->i0 == 0) {
				if (object->i1 < fake_script.reader_frames) {
					struct fake_object *sample = new_object("CMSampleBuffer", FK_SBUF);

					object->i1++;
					sample->samples = 1;
					sample->image = (id)pixel_buffer_new(640, 480, N2_PIXFMT_420V);
					return (void *)sample;
				}
				return NULL;
			}
			if (object->i1 < fake_script.reader_audio_samples) {
				struct fake_object *sample = new_object("CMSampleBuffer", FK_SBUF);
				long count = fake_script.reader_audio_samples - object->i1;

				if (count > 1024) {
					count = 1024;
				}
				object->i1 += count;
				sample->samples = count;
				return (void *)sample;
			}
			return NULL;
		}
		break;
	case FK_SESSION:
		if (selector_is(selector, "canSetSessionPreset:")) {
			return (void *)1;
		}
		if (selector_is(selector, "setSessionPreset:")) {
			snprintf(object->text, sizeof(object->text), "%s", fake_text((id)a1));
			return NULL;
		}
		if (selector_is(selector, "canAddInput:")) {
			return (void *)1;
		}
		if (selector_is(selector, "addInput:")) {
			object->i0++;
			return NULL;
		}
		if (selector_is(selector, "canAddOutput:")) {
			return (void *)1;
		}
		if (selector_is(selector, "addOutput:")) {
			object->p0 = a1;
			return NULL;
		}
		if (selector_is(selector, "startRunning")) {
			return NULL;
		}
		if (selector_is(selector, "stopRunning")) {
			return NULL;
		}
		break;
	case FK_MOVIE:
		if (selector_is(selector, "startRecordingToOutputFileURL:recordingDelegate:")) {
			object->p0 = a1;
			object->p1 = a2;
			fake_stats.capture_start_calls++;
			return NULL;
		}
		if (selector_is(selector, "stopRecording")) {
			fake_stats.capture_stop_calls++;
			{
				char path[512];
				strncpy(path, fake_text((id)object->p0), sizeof(path) - 1);
				path[sizeof(path) - 1] = 0;
				if (fake_script.capture_success) {
					int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

					if (fd >= 0) {
						char payload[1000];

						memset(payload, 'N', sizeof(payload));
						if (write(fd, payload, sizeof(payload)) < 0) {
							/* the test only cares that the file exists */
						}
						close(fd);
					}
					((void (*)(id, SEL, id, id, id, id))objc_msgSend)(
						object->p1, N2O_SEL("captureOutput:didFinishRecordingToOutputFileAtURL:fromConnections:error:"),
						(id)object, object->p0, NULL, NULL);
				} else {
					((void (*)(id, SEL, id, id, id, id))objc_msgSend)(
						object->p1, N2O_SEL("captureOutput:didFinishRecordingToOutputFileAtURL:fromConnections:error:"),
						(id)object, object->p0, NULL, make_error("scripted capture failure"));
				}
				fake_stats.capture_delegate_calls++;
			}
			return NULL;
		}
		break;
	case FK_DEVICE:
	case FK_DEVINPUT:
	case FK_POOL:
	case FK_DELEGATE:
	case FK_PLAIN:
	case FK_SBUF:
		break;
	}
	return NULL;
}

void *
fake_objc_dispatch_saved(void)
{
	id self = (id)fake_regs[0];
	const char *selector = (const char *)fake_regs[1];

	return dispatch_message(self, selector, fake_regs[2], fake_regs[3], fake_regs[4], fake_regs[5], fake_regs[6]);
}

void
fake_objc_dispatch_stret_saved(void)
{
	CMTime *result = (CMTime *)fake_regs[0];
	id self = (id)fake_regs[1];
	const char *selector = (const char *)fake_regs[2];
	struct fake_object *object = object_of(self);

	record_message(object != NULL ? object->cls : "(stret)", selector, 0);
	if (result != NULL) {
		CMTime duration = { 120, 60, 1, 0 };

		if (!selector_is(selector, "duration")) {
			duration = (CMTime){ 0, 1, 0, 0 };
		}
		memcpy(result, &duration, sizeof(duration));
	}
}

/*
 * The receiver and argument registers are saved in the dispatcher's globals; the C dispatcher above copies what it
 * needs before any nested objc_msgSend (the delegate's -description/-path calls). xmm0/xmm1 carry CGSize and float
 * results back (a float is placed in the low 32 bits of fake_ret_d0, which is what a caller reading xmm0 sees).
 */
void *fake_regs[8];
double fake_ret_d0;
double fake_ret_d1;

__asm__(
	".text\n"
	".p2align 4\n"
	".globl objc_msgSend\n"
	"objc_msgSend:\n"
	"  movq %rdi, fake_regs+0(%rip)\n"
	"  movq %rsi, fake_regs+8(%rip)\n"
	"  movq %rdx, fake_regs+16(%rip)\n"
	"  movq %rcx, fake_regs+24(%rip)\n"
	"  movq %r8,  fake_regs+32(%rip)\n"
	"  movq %r9,  fake_regs+40(%rip)\n"
	"  movq %rsp, fake_regs+48(%rip)\n"
	"  subq $8, %rsp\n"
	"  call fake_objc_dispatch_saved\n"
	"  addq $8, %rsp\n"
	"  movsd fake_ret_d0(%rip), %xmm0\n"
	"  movsd fake_ret_d1(%rip), %xmm1\n"
	"  ret\n"
	".p2align 4\n"
	".globl objc_msgSend_stret\n"
	"objc_msgSend_stret:\n"
	"  movq %rdi, fake_regs+0(%rip)\n"
	"  movq %rsi, fake_regs+8(%rip)\n"
	"  movq %rdx, fake_regs+16(%rip)\n"
	"  movq %rcx, fake_regs+24(%rip)\n"
	"  movq %r8,  fake_regs+32(%rip)\n"
	"  movq %r9,  fake_regs+40(%rip)\n"
	"  movq %rsp, fake_regs+48(%rip)\n"
	"  subq $8, %rsp\n"
	"  call fake_objc_dispatch_stret_saved\n"
	"  addq $8, %rsp\n"
	"  ret\n"
);

/* ---- libSystem entry points the probe's pool of the fake needs ---------------------------------------------- */

void *
objc_autoreleasePoolPush(void)
{
	static char pool;

	return &pool;
}

void
objc_autoreleasePoolPop(void *pool)
{
	(void)pool;
}

dispatch_semaphore_t
dispatch_semaphore_create(long value)
{
	static long counter;

	counter = value;
	return (dispatch_semaphore_t)&counter;
}

long
dispatch_semaphore_wait(dispatch_semaphore_t semaphore, uint64_t timeout)
{
	long *counter = (long *)semaphore;

	(void)timeout;
	if (counter == NULL) {
		return -1;
	}
	if (*counter > 0) {
		(*counter)--;
		return 0;
	}
	return 49;      /* dispatch_timeout: the test script always signals */
}

long
dispatch_semaphore_signal(dispatch_semaphore_t semaphore)
{
	long *counter = (long *)semaphore;

	if (counter == NULL) {
		return -1;
	}
	(*counter)++;
	return 0;
}

uint64_t
dispatch_time(uint64_t when, int64_t delta)
{
	(void)when;
	(void)delta;
	return (uint64_t)-1;
}

void *
_NSConcreteGlobalBlock;

const CFAllocatorRef kCFAllocatorNull = NULL;

/* ---- dlsym / dlopen of the framework and its constants (host replacements) --------------------------------- */

void *
n2fake_dlopen(const char *path, int mode)
{
	(void)mode;
	if (framework_count < FMAX_FRAMEWORKS) {
		snprintf(frameworks[framework_count++], sizeof(frameworks[0]), "%s", path);
	}
	return (void *)(intptr_t)1;
}

void *
n2fake_dlsym(void *handle, const char *name)
{
	(void)handle;
	if (symbol_request_count < FMAX_SYMBOLS) {
		snprintf(symbol_requests[symbol_request_count++], sizeof(symbol_requests[0]), "%s", name);
	}
	for (unsigned i = 0; i < sizeof(constants) / sizeof(constants[0]); i++) {
		if (strcmp(constants[i].name, name) == 0) {
			if (constants[i].object == NULL) {
				constants[i].object = make_string(constants[i].value);
			}
			return &constants[i].object;
		}
	}
	fake_stats.dlsym_misses++;
	return NULL;
}

/* ---- inspection API for the test --------------------------------------------------------------------------- */

void
fake_objc_reset(void)
{
	object_count = 0;
	record_count = 0;
	class_request_count = 0;
	symbol_request_count = 0;
	framework_count = 0;
	block_buffer_count = 0;
	memset(&fake_stats, 0, sizeof(fake_stats));
	fake_stats.class_request_count = 0;
	fake_script.start_writing = 1;
	fake_script.writer_error = NULL;
	fake_script.writer_status = 2;
	fake_script.append_result = 1;
	fake_script.finish_calls_block = 1;
	fake_script.capture_authorization = 3;
	fake_script.capture_success = 1;
	fake_script.reader_frames = 60;
	fake_script.reader_audio_samples = 88200;
	fake_script.reader_status = 2;
	writer_url_text[0] = 0;
	writer_file_type_text[0] = 0;
	for (unsigned i = 0; i < sizeof(constants) / sizeof(constants[0]); i++) {
		constants[i].object = NULL;
	}
	for (int i = 0; i < FMAX_PIXEL_BUFFERS; i++) {
		if (pixel_buffers[i] != NULL) {
			pixel_buffers[i]->in_use = 0;
		}
	}
	dynamic_class.registered = 0;
	dynamic_class.method_count = 0;
}

void
fake_objc_clear_records(void)
{
	record_count = 0;
}

int
fake_objc_count(void)
{
	return record_count;
}

const char *
fake_objc_class_at(int index)
{
	return index >= 0 && index < record_count ? records[index].cls : "";
}

const char *
fake_objc_selector_at(int index)
{
	return index >= 0 && index < record_count ? records[index].selector : "";
}

int
fake_objc_is_class_at(int index)
{
	return index >= 0 && index < record_count ? records[index].is_class : 0;
}

int
fake_objc_has_selector(const char *selector)
{
	for (int i = 0; i < record_count; i++) {
		if (strcmp(records[i].selector, selector) == 0) {
			return 1;
		}
	}
	return 0;
}

int
fake_objc_class_request_count(void)
{
	return class_request_count;
}

const char *
fake_objc_class_request_at(int index)
{
	return index >= 0 && index < class_request_count ? class_requests[index] : "";
}

int
fake_objc_symbol_count(void)
{
	return symbol_request_count;
}

const char *
fake_objc_symbol_at(int index)
{
	return index >= 0 && index < symbol_request_count ? symbol_requests[index] : "";
}

int
fake_objc_framework_count(void)
{
	return framework_count;
}

const char *
fake_objc_framework_at(int index)
{
	return index >= 0 && index < framework_count ? frameworks[index] : "";
}

id
fake_find_class_object(const char *class_name, int index)
{
	int seen = 0;

	for (int i = 0; i < object_count; i++) {
		if (strcmp(objects[i].cls, class_name) == 0) {
			if (seen == index) {
				return (id)&objects[i];
			}
			seen++;
		}
	}
	return NULL;
}

id
fake_find_dictionary(const char *key_text)
{
	for (int i = 0; i < object_count; i++) {
		struct fake_object *object = &objects[i];

		if (object->kind != FK_DICT) {
			continue;
		}
		for (int k = 0; k < object->dict_count; k++) {
			if (strcmp(fake_text(object->dict_keys[k]), key_text) == 0) {
				return (id)object;
			}
		}
	}
	return NULL;
}

id
fake_dict_get(id dictionary, const char *key_text)
{
	struct fake_object *object = object_of(dictionary);

	if (object == NULL || object->kind != FK_DICT) {
		return NULL;
	}
	for (int i = 0; i < object->dict_count; i++) {
		if (strcmp(fake_text(object->dict_keys[i]), key_text) == 0) {
			return object->dict_values[i];
		}
	}
	return NULL;
}

int
fake_dict_count(id dictionary)
{
	struct fake_object *object = object_of(dictionary);

	return object != NULL && object->kind == FK_DICT ? object->dict_count : 0;
}

const char *
fake_text(id object_id)
{
	struct fake_object *object = object_of(object_id);

	if (object == NULL) {
		return "";
	}
	switch (object->kind) {
	case FK_STRING:
	case FK_URL:
	case FK_ERROR:
	case FK_TRACK:
	case FK_DEVICE:
	case FK_INPUT:
	case FK_SESSION:
		return object->text;
	default:
		return "";
	}
}

long
fake_number(id object_id)
{
	struct fake_object *object = object_of(object_id);

	return object != NULL && object->kind == FK_NUMBER ? object->number : 0;
}

int
fake_realtime(id object_id)
{
	struct fake_object *object = object_of(object_id);

	return object != NULL && object->kind == FK_INPUT ? object->i0 : -1;
}

const char *
fake_writer_url(void)
{
	return writer_url_text;
}

const char *
fake_writer_file_type(void)
{
	return writer_file_type_text;
}
