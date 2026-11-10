/* SPDX-License-Identifier: MIT */
/*
 * probe_av.c -- the AVFoundation half of the Nord 2 video probe: T10/T10b (AVAssetWriter writes an MP4/MOV with a
 * video track encoded by VideoToolbox and an AAC audio track from 16-bit LPCM sample buffers), T11 (AVAssetReader
 * reads the file back: frame count, first-picture luma, audio sample count) and T12/T12a (a headless
 * AVCaptureSession + AVCaptureMovieFileOutput recording, with a delegate class built at run time).
 *
 * Everything Apple is spoken to through the Objective-C runtime (probe_objc.h); no Apple SDK header is used and no
 * symbol of AVFoundation is imported by name -- the framework is dlopen()ed, the classes are found with
 * objc_getClass(), and every AVFileType* / AVMediaType* / AVVideo*Key constant is read from the image's exported
 * variable with dlsym(RTLD_DEFAULT, name). Only when dlsym() gives NULL does the literal fallback in this file
 * apply, and that event is reported as "<test>.const.<symbol>=literal:<value>".
 *
 * Paths. The probe runs from launchd as root; the installer makes /private/var/db/nord2-video mode 1777. The tests
 * write there. N2_PROBE_DB overrides the /private/var/db prefix so the host oracle can run the same code against a
 * temporary directory; on the phone the variable is unset and the task's paths are used exactly.
 *
 * On exceptions: this code checks every message that can return nil and reports which one did, but it cannot catch
 * an Objective-C exception (there is no @try in C). probe_core.c runs each test in a forked child with a timeout,
 * so an exception's abort is contained and reported by the runner as "crash". That is the design of the probe,
 * not a substitute for the nil checks here.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>

#include "n2_apple.h"
#include "probe_objc.h"
#include "probe_core.h"

#define N2O_PI 3.14159265358979323846

/* ---- reporting --------------------------------------------------------------------------------------------- */

static void
sayf(struct probe_ctx *c, const char *prefix, const char *field, const char *format, ...)
{
	char key[128];
	char value[512];
	va_list ap;

	snprintf(key, sizeof(key), "%s.%s", prefix, field);
	va_start(ap, format);
	vsnprintf(value, sizeof(value), format, ap);
	va_end(ap);
	probe_say(c, key, "%s", value);
}

static const char *
database_dir(void)
{
	const char *env = getenv("N2_PROBE_DB");

	return env != NULL && env[0] != 0 ? env : "/private/var/db";
}

static void
movie_path(char *buf, size_t size, const char *name)
{
	snprintf(buf, size, "%s/nord2-video/%s", database_dir(), name);
}

static long
file_size(const char *path)
{
	int fd = open(path, O_RDONLY);
	long size;

	if (fd < 0) {
		return -1;
	}
	size = lseek(fd, 0, SEEK_END);
	close(fd);
	return size;
}

/* ---- the Apple objects ------------------------------------------------------------------------------------- */

static int
load_frameworks(struct probe_ctx *c, const char *prefix)
{
	static const char *const paths[] = {
		"/System/Library/Frameworks/AVFoundation.framework/AVFoundation",
		"/System/Library/Frameworks/CoreMedia.framework/CoreMedia",
		"/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox",
	};
	int loaded = 0;

	for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		void *handle = dlopen(paths[i], RTLD_LAZY | RTLD_GLOBAL);

		if (handle != NULL) {
			loaded++;
		} else {
			sayf(c, prefix, "framework", "absent:%s", paths[i]);
		}
	}
	sayf(c, prefix, "frameworks", "%d", loaded);
	return loaded == (int)(sizeof(paths) / sizeof(paths[0])) ? 0 : 1;
}

/*
 * The VALUE of an exported NSString constant: dlsym returns the address of the pointer variable, so the object is
 * *(id *)slot. The literal is used only when the symbol or its value is absent, and then it is reported.
 */
static id
constant_id(struct probe_ctx *c, const char *prefix, const char *name, const char *fallback)
{
	char key[160];
	void *slot = dlsym(RTLD_DEFAULT, name);
	id string_class = (id)objc_getClass("NSString");

	if (slot != NULL) {
		id value = *(id *)slot;

		if (value != NULL) {
			return value;
		}
	}
	snprintf(key, sizeof(key), "%s.const.%s", prefix, name);
	probe_say(c, key, "literal:%s", fallback);
	if (string_class == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, const char *))objc_msgSend)(string_class, N2O_SEL("stringWithUTF8String:"), fallback);
}

static id
nsstr(const char *text)
{
	id string_class = (id)objc_getClass("NSString");

	if (string_class == NULL || text == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, const char *))objc_msgSend)(string_class, N2O_SEL("stringWithUTF8String:"), text);
}

static id
nsurl_path(const char *path)
{
	id url_class = (id)objc_getClass("NSURL");
	id string = nsstr(path);

	if (url_class == NULL || string == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, id))objc_msgSend)(url_class, N2O_SEL("fileURLWithPath:"), string);
}

static id
nsnum(int value)
{
	id number_class = (id)objc_getClass("NSNumber");

	if (number_class == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, int))objc_msgSend)(number_class, N2O_SEL("numberWithInt:"), value);
}

static id
nsbool(int value)
{
	id number_class = (id)objc_getClass("NSNumber");

	if (number_class == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, BOOL))objc_msgSend)(number_class, N2O_SEL("numberWithBool:"), (BOOL)(value != 0));
}

static id
make_dict(id *objects, id *keys, unsigned long count)
{
	id dictionary_class = (id)objc_getClass("NSDictionary");

	if (dictionary_class == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, const id *, const id *, unsigned long))objc_msgSend)(
		dictionary_class, N2O_SEL("dictionaryWithObjects:forKeys:count:"), objects, keys, count);
}

static int
any_null(id *values, unsigned count)
{
	for (unsigned i = 0; i < count; i++) {
		if (values[i] == NULL) {
			return 1;
		}
	}
	return 0;
}

static id
new_object(const char *class_name)
{
	id cls = (id)objc_getClass(class_name);
	id object;

	if (cls == NULL) {
		return NULL;
	}
	object = ((id (*)(id, SEL))objc_msgSend)(cls, N2O_SEL("alloc"));
	return object != NULL ? ((id (*)(id, SEL))objc_msgSend)(object, N2O_SEL("init")) : NULL;
}

static void
error_text(id error, char *buf, size_t size)
{
	id description;
	const char *text;

	buf[0] = 0;
	if (error == NULL) {
		return;
	}
	description = ((id (*)(id, SEL))objc_msgSend)(error, N2O_SEL("description"));
	text = n2o_utf8(description);
	if (text != NULL) {
		snprintf(buf, size, "%s", text);
	}
}

/* ---- video pictures ---------------------------------------------------------------------------------------- */

static int
fill_video_picture(struct probe_ctx *c, const char *prefix, CVPixelBufferRef buffer, int frame)
{
	size_t width = CVPixelBufferGetWidth(buffer);
	size_t height = CVPixelBufferGetHeight(buffer);
	size_t planes = CVPixelBufferGetPlaneCount(buffer);
	uint8_t *luma;
	uint8_t *chroma;
	size_t luma_stride, chroma_stride;

	if (CVPixelBufferGetPixelFormatType(buffer) != N2_PIXFMT_420V) {
		sayf(c, prefix, "format", "0x%08x", (unsigned)CVPixelBufferGetPixelFormatType(buffer));
		return -1;
	}
	if (CVPixelBufferLockBaseAddress(buffer, 0) != 0) {
		sayf(c, prefix, "lock", "0");
		return -1;
	}
	luma = CVPixelBufferGetBaseAddressOfPlane(buffer, 0);
	chroma = planes > 1 ? CVPixelBufferGetBaseAddressOfPlane(buffer, 1) : NULL;
	luma_stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
	chroma_stride = planes > 1 ? CVPixelBufferGetBytesPerRowOfPlane(buffer, 1) : 0;
	if (luma == NULL || chroma == NULL || width == 0 || height == 0) {
		sayf(c, prefix, "plane", "0");
		CVPixelBufferUnlockBaseAddress(buffer, 0);
		return -1;
	}
	probe_fill_nv12(luma, luma_stride, chroma, chroma_stride, (int)width, (int)height, frame);
	CVPixelBufferUnlockBaseAddress(buffer, 0);
	return 0;
}

static CVPixelBufferRef
make_video_picture(struct probe_ctx *c, const char *prefix, id adaptor, int width, int height)
{
	CVPixelBufferRef buffer = NULL;

	if (adaptor != NULL) {
		id pool = ((id (*)(id, SEL))objc_msgSend)(adaptor, N2O_SEL("pixelBufferPool"));

		if (pool != NULL) {
			if (CVPixelBufferPoolCreatePixelBuffer(NULL, (CVPixelBufferPoolRef)pool, &buffer) != 0) {
				buffer = NULL;
			}
		}
	}
	if (buffer == NULL) {
		if (CVPixelBufferCreate(NULL, (size_t)width, (size_t)height, N2_PIXFMT_420V, NULL, &buffer) != 0) {
			sayf(c, prefix, "pixelbuffer", "0");
			return NULL;
		}
	}
	return buffer;
}

static int
wait_until_ready(struct probe_ctx *c, const char *prefix, id input, const char *what)
{
	for (unsigned i = 0; i < 2500; i++) {
		BOOL ready = ((BOOL (*)(id, SEL))objc_msgSend)(input, N2O_SEL("isReadyForMoreMediaData"));

		if (ready) {
			return 0;
		}
		usleep(2000);
	}
	sayf(c, prefix, "ready", "%s:timeout", what);
	return -1;
}

/* ---- audio: 2 s of a 440 Hz sine as 16-bit mono LPCM CMSampleBuffers ---------------------------------------- */

static int
append_sine_audio(struct probe_ctx *c, const char *prefix, id audio_input)
{
	enum { TOTAL_SAMPLES = 88200, CHUNK = 1024 };
	static int16_t samples[TOTAL_SAMPLES];
	static size_t sample_sizes[CHUNK];
	static int generated;
	struct n2o_asbd asbd;
	CMAudioFormatDescriptionRef format = NULL;
	OSStatus status;
	int64_t done = 0;
	int buffers = 0;
	int failed = 0;

	if (!generated) {
		const double step = 2.0 * N2O_PI * 440.0 / 44100.0;

		for (int i = 0; i < TOTAL_SAMPLES; i++) {
			samples[i] = (int16_t)(12000.0 * sin(step * (double)i));
		}
		for (int i = 0; i < CHUNK; i++) {
			sample_sizes[i] = 2;      /* one 16-bit mono frame */
		}
		generated = 1;
	}
	memset(&asbd, 0, sizeof(asbd));
	asbd.mSampleRate = 44100.0;
	asbd.mFormatID = N2O_FORMAT_LPCM;
	asbd.mFormatFlags = N2O_FLAG_SIGNED_INTEGER | N2O_FLAG_PACKED;
	asbd.mBytesPerPacket = 2;
	asbd.mFramesPerPacket = 1;
	asbd.mBytesPerFrame = 2;
	asbd.mChannelsPerFrame = 1;
	asbd.mBitsPerChannel = 16;
	status = CMAudioFormatDescriptionCreate(NULL, &asbd, 0, NULL, 0, NULL, NULL, &format);
	if (status != 0 || format == NULL) {
		sayf(c, prefix, "audio.format", "%d", (int)status);
		if (format != NULL) {
			CFRelease(format);
		}
		return 1;
	}
	while (done < TOTAL_SAMPLES) {
		CMBlockBufferRef block = NULL;
		CMSampleBufferRef sample = NULL;
		CMSampleTimingInfo timing;
		int64_t count = TOTAL_SAMPLES - done;

		if (count > CHUNK) {
			count = CHUNK;
		}
		status = CMBlockBufferCreateWithMemoryBlock(NULL, &samples[done], (size_t)count * 2, kCFAllocatorNull,
		                                            NULL, 0, (size_t)count * 2, 0, &block);
		if (status != 0 || block == NULL) {
			sayf(c, prefix, "audio.block", "%d", (int)status);
			failed = 1;
			break;
		}
		timing.duration = n2o_time(count, 44100);
		timing.presentationTimeStamp = n2o_time(done, 44100);
		timing.decodeTimeStamp = (CMTime){ 0, 1, 0, 0 };
		status = CMSampleBufferCreateReady(NULL, block, (CMFormatDescriptionRef)format, (CMItemCount)count, 1,
		                                   &timing, (CMItemCount)count, sample_sizes, &sample);
		if (status != 0 || sample == NULL) {
			sayf(c, prefix, "audio.sample", "%d", (int)status);
			CFRelease(block);
			failed = 1;
			break;
		}
		if (wait_until_ready(c, prefix, audio_input, "audio") != 0) {
			CFRelease(sample);
			CFRelease(block);
			failed = 1;
			break;
		}
		{
			BOOL appended = ((BOOL (*)(id, SEL, CMSampleBufferRef))objc_msgSend)(
				audio_input, N2O_SEL("appendSampleBuffer:"), sample);

			if (!appended) {
				sayf(c, prefix, "append.audio.fail", "%d", buffers);
				failed = 1;
			}
		}
		CFRelease(sample);
		CFRelease(block);
		if (failed) {
			break;
		}
		done += count;
		buffers++;
	}
	CFRelease(format);
	sayf(c, prefix, "audio.buffers", "%d", buffers);
	sayf(c, prefix, "audio.samples", "%lld", (long long)done);
	return failed ? 1 : 0;
}

/* ---- T10/T10b: AVAssetWriter writing a video + AAC file ---------------------------------------------------- */

static dispatch_semaphore_t g_finish_semaphore;
static volatile long g_finish_called;

N2O_GLOBAL_BLOCK(t10_finish_block, (void *block), {
	(void)block;
	g_finish_called = 1;
	if (g_finish_semaphore != NULL) {
		dispatch_semaphore_signal(g_finish_semaphore);
	}
});

static int
write_movie(struct probe_ctx *c, const char *prefix, const char *path, const char *codec,
            const char *file_type_symbol, const char *file_type_literal)
{
	enum { WIDTH = 640, HEIGHT = 480, FRAMES = 60 };
	void *pool;
	id file_type, media_video, media_audio, codec_string;
	id video_keys[4], video_values[4], video_settings;
	id audio_keys[4], audio_values[4], audio_settings;
	id attribute_keys[3], attribute_values[3], attributes;
	id url, error = NULL, writer = NULL, video_input = NULL, audio_input = NULL, adaptor = NULL;
	id writer_class, input_class, adaptor_class;
	int video_appended = 0;
	int video_failed = 0;
	int audio_failed = 1;
	int finish_woken = 0;
	long status_value = -1;
	long bytes;
	long started;
	char error_buffer[512];
	int result = 1;

	unlink(path);
	pool = objc_autoreleasePoolPush();
	if (load_frameworks(c, prefix) != 0) {
		objc_autoreleasePoolPop(pool);
		return 1;
	}
	file_type = constant_id(c, prefix, file_type_symbol, file_type_literal);
	media_video = constant_id(c, prefix, "AVMediaTypeVideo", "vide");
	media_audio = constant_id(c, prefix, "AVMediaTypeAudio", "soun");
	codec_string = nsstr(codec);
	writer_class = (id)objc_getClass("AVAssetWriter");
	input_class = (id)objc_getClass("AVAssetWriterInput");
	adaptor_class = (id)objc_getClass("AVAssetWriterInputPixelBufferAdaptor");
	url = nsurl_path(path);
	if (file_type == NULL) {
		sayf(c, prefix, "const.filetype", "0");
		goto out;
	}
	if (media_video == NULL) {
		sayf(c, prefix, "const.media.video", "0");
		goto out;
	}
	if (media_audio == NULL) {
		sayf(c, prefix, "const.media.audio", "0");
		goto out;
	}
	if (codec_string == NULL) {
		sayf(c, prefix, "string.codec", "0");
		goto out;
	}
	if (writer_class == NULL || input_class == NULL || adaptor_class == NULL) {
		sayf(c, prefix, "class.writer", "%d", writer_class != NULL && input_class != NULL && adaptor_class != NULL);
		goto out;
	}
	if (url == NULL) {
		sayf(c, prefix, "url", "0");
		goto out;
	}

	video_keys[0] = constant_id(c, prefix, "AVVideoCodecKey", "AVVideoCodecKey");
	video_keys[1] = constant_id(c, prefix, "AVVideoWidthKey", "AVVideoWidthKey");
	video_keys[2] = constant_id(c, prefix, "AVVideoHeightKey", "AVVideoHeightKey");
	video_keys[3] = constant_id(c, prefix, "AVVideoCompressionPropertiesKey", "AVVideoCompressionPropertiesKey");
	video_values[0] = codec_string;
	video_values[1] = nsnum(WIDTH);
	video_values[2] = nsnum(HEIGHT);
	{
		id bitrate_key = constant_id(c, prefix, "AVVideoAverageBitRateKey", "AVVideoAverageBitRateKey");
		id bitrate_value = nsnum(1000000);
		id objects[1] = { bitrate_value };
		id keys[1] = { bitrate_key };

		if (any_null(objects, 1) || any_null(keys, 1)) {
			sayf(c, prefix, "video.bitrate", "0");
			goto out;
		}
		video_values[3] = make_dict(objects, keys, 1);
	}
	if (any_null(video_keys, 4) || any_null(video_values, 4)) {
		sayf(c, prefix, "video.values", "0");
		goto out;
	}
	video_settings = make_dict(video_values, video_keys, 4);
	if (video_settings == NULL) {
		sayf(c, prefix, "video.settings", "0");
		goto out;
	}
	sayf(c, prefix, "video.settings", "1");

	audio_keys[0] = constant_id(c, prefix, "AVFormatIDKey", "AVFormatIDKey");
	audio_keys[1] = constant_id(c, prefix, "AVSampleRateKey", "AVSampleRateKey");
	audio_keys[2] = constant_id(c, prefix, "AVNumberOfChannelsKey", "AVNumberOfChannelsKey");
	audio_keys[3] = constant_id(c, prefix, "AVEncoderBitRateKey", "AVEncoderBitRateKey");
	audio_values[0] = nsnum(1633772320);      /* 'aac ' */
	audio_values[1] = nsnum(44100);
	audio_values[2] = nsnum(1);
	audio_values[3] = nsnum(64000);
	if (any_null(audio_keys, 4) || any_null(audio_values, 4)) {
		sayf(c, prefix, "audio.values", "0");
		goto out;
	}
	audio_settings = make_dict(audio_values, audio_keys, 4);
	if (audio_settings == NULL) {
		sayf(c, prefix, "audio.settings", "0");
		goto out;
	}
	sayf(c, prefix, "audio.settings", "1");

	writer = ((id (*)(id, SEL, id, id, id *))objc_msgSend)(
		writer_class, N2O_SEL("assetWriterWithURL:fileType:error:"), url, file_type, &error);
	if (writer == NULL) {
		error_text(error, error_buffer, sizeof(error_buffer));
		sayf(c, prefix, "writer", "0");
		sayf(c, prefix, "error", "%s", error_buffer);
		goto out;
	}
	sayf(c, prefix, "writer", "1");

	video_input = ((id (*)(id, SEL, id, id))objc_msgSend)(
		input_class, N2O_SEL("assetWriterInputWithMediaType:outputSettings:"), media_video, video_settings);
	if (video_input == NULL) {
		sayf(c, prefix, "input.video", "0");
		goto out;
	}
	sayf(c, prefix, "input.video", "1");
	((void (*)(id, SEL, BOOL))objc_msgSend)(video_input, N2O_SEL("setExpectsMediaDataInRealTime:"), (BOOL)1);   /* (lead) real time: no interleaving wait between the two inputs */

	audio_input = ((id (*)(id, SEL, id, id))objc_msgSend)(
		input_class, N2O_SEL("assetWriterInputWithMediaType:outputSettings:"), media_audio, audio_settings);
	if (audio_input == NULL) {
		sayf(c, prefix, "input.audio", "0");
		goto out;
	}
	sayf(c, prefix, "input.audio", "1");
	((void (*)(id, SEL, BOOL))objc_msgSend)(audio_input, N2O_SEL("setExpectsMediaDataInRealTime:"), (BOOL)1);   /* (lead) real time: no interleaving wait between the two inputs */

	attribute_keys[0] = constant_id(c, prefix, "kCVPixelBufferPixelFormatTypeKey", "PixelFormatType");
	attribute_keys[1] = constant_id(c, prefix, "kCVPixelBufferWidthKey", "Width");
	attribute_keys[2] = constant_id(c, prefix, "kCVPixelBufferHeightKey", "Height");
	attribute_values[0] = nsnum((int)N2_PIXFMT_420V);
	attribute_values[1] = nsnum(WIDTH);
	attribute_values[2] = nsnum(HEIGHT);
	if (any_null(attribute_keys, 3) || any_null(attribute_values, 3)) {
		sayf(c, prefix, "attribute.values", "0");
		goto out;
	}
	attributes = make_dict(attribute_values, attribute_keys, 3);
	adaptor = ((id (*)(id, SEL, id, id))objc_msgSend)(
		adaptor_class, N2O_SEL("assetWriterInputPixelBufferAdaptorWithAssetWriterInput:sourcePixelBufferAttributes:"),
		video_input, attributes);
	if (adaptor == NULL) {
		sayf(c, prefix, "adaptor", "0");
		goto out;
	}
	sayf(c, prefix, "adaptor", "1");

	{
		BOOL can_video = ((BOOL (*)(id, SEL, id))objc_msgSend)(writer, N2O_SEL("canAddInput:"), video_input);
		BOOL can_audio;

		sayf(c, prefix, "canAdd.video", "%d", (int)can_video);
		if (!can_video) {
			goto out;
		}
		((void (*)(id, SEL, id))objc_msgSend)(writer, N2O_SEL("addInput:"), video_input);
		sayf(c, prefix, "add.video", "1");
		can_audio = ((BOOL (*)(id, SEL, id))objc_msgSend)(writer, N2O_SEL("canAddInput:"), audio_input);
		sayf(c, prefix, "canAdd.audio", "%d", (int)can_audio);
		if (!can_audio) {
			goto out;
		}
		((void (*)(id, SEL, id))objc_msgSend)(writer, N2O_SEL("addInput:"), audio_input);
		sayf(c, prefix, "add.audio", "1");
	}

	started = ((BOOL (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("startWriting"));
	sayf(c, prefix, "startWriting", "%ld", started);
	if (!started) {
		error = ((id (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("error"));
		error_text(error, error_buffer, sizeof(error_buffer));
		sayf(c, prefix, "error", "%s", error_buffer);
		status_value = ((long (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("status"));
		sayf(c, prefix, "status", "%ld", status_value);
		goto out;
	}
	/* (lead) run 2 on the phone: appending without a session makes AVFoundation throw (abort in
	 * -[AVFigAssetWriterTrack addPixelBuffer:atPresentationTime:error:]). */
	((void (*)(id, SEL, CMTime))objc_msgSend)(writer, N2O_SEL("startSessionAtSourceTime:"), n2o_time(0, 30));
	sayf(c, prefix, "startSession", "1");

	for (int frame = 0; frame < FRAMES; frame++) {
		CVPixelBufferRef picture;
		BOOL appended;

		if (wait_until_ready(c, prefix, video_input, "video") != 0) {
			video_failed = 1;
			break;
		}
		picture = make_video_picture(c, prefix, adaptor, WIDTH, HEIGHT);
		if (picture == NULL) {
			video_failed = 1;
			break;
		}
		if (fill_video_picture(c, prefix, picture, frame) != 0) {
			CVPixelBufferRelease(picture);
			video_failed = 1;
			break;
		}
		appended = ((BOOL (*)(id, SEL, CVPixelBufferRef, CMTime))objc_msgSend)(
			adaptor, N2O_SEL("appendPixelBuffer:withPresentationTime:"), picture, n2o_time(frame, 30));
		CVPixelBufferRelease(picture);
		if (!appended) {
			video_failed = 1;
			break;
		}
		video_appended++;
	}
	sayf(c, prefix, "append.video", "%d", video_appended);
	if (video_failed || video_appended != FRAMES) {
		goto out;
	}

	audio_failed = append_sine_audio(c, prefix, audio_input);
	if (audio_failed) {
		goto out;
	}

	((void (*)(id, SEL))objc_msgSend)(video_input, N2O_SEL("markAsFinished"));
	((void (*)(id, SEL))objc_msgSend)(audio_input, N2O_SEL("markAsFinished"));
	sayf(c, prefix, "markAsFinished", "2");

	g_finish_called = 0;
	g_finish_semaphore = dispatch_semaphore_create(0);
	if (g_finish_semaphore == NULL) {
		sayf(c, prefix, "finish", "0");
		goto out;
	}
	((void (*)(id, SEL, id))objc_msgSend)(writer, N2O_SEL("finishWritingWithCompletionHandler:"),
	                                      (id)&t10_finish_block);
	finish_woken = dispatch_semaphore_wait(g_finish_semaphore, dispatch_time(N2O_TIME_NOW, N2O_TIME_SECONDS(15))) == 0;
	sayf(c, prefix, "finish", "%d", finish_woken);
	status_value = ((long (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("status"));
	error = ((id (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("error"));
	error_text(error, error_buffer, sizeof(error_buffer));
	sayf(c, prefix, "status", "%ld", status_value);
	sayf(c, prefix, "error", "%s", error_buffer);
	bytes = file_size(path);
	sayf(c, prefix, "bytes", "%ld", bytes);
	if (finish_woken && status_value == 2 && bytes > 0) {
		result = 0;
	}

out:
	if (result != 0) {
		if (writer != NULL) {
			status_value = ((long (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("status"));
			error = ((id (*)(id, SEL))objc_msgSend)(writer, N2O_SEL("error"));
			error_text(error, error_buffer, sizeof(error_buffer));
			sayf(c, prefix, "status", "%ld", status_value);
			sayf(c, prefix, "error", "%s", error_buffer);
		}
		bytes = file_size(path);
		sayf(c, prefix, "bytes", "%ld", bytes);
	}
	objc_autoreleasePoolPop(pool);
	return result;
}

static int
t10_write_h264_aac(struct probe_ctx *c)
{
	char path[512];

	movie_path(path, sizeof(path), "t10.mp4");
	return write_movie(c, "t10", path, "avc1", "AVFileTypeMPEG4", "public.mpeg-4");
}

static int
t10b_write_hevc(struct probe_ctx *c)
{
	char path[512];

	movie_path(path, sizeof(path), "t10b.mov");
	return write_movie(c, "t10b", path, "hvc1", "AVFileTypeQuickTimeMovie", "com.apple.quicktime-movie");
}

/* ---- T11: AVAssetReader reads a file back ------------------------------------------------------------------ */

static double
mean_luma(CVPixelBufferRef picture)
{
	size_t width = CVPixelBufferGetWidth(picture);
	size_t height = CVPixelBufferGetHeight(picture);
	size_t stride;
	uint8_t *base;
	uint64_t sum = 0;

	if (width == 0 || height == 0 || CVPixelBufferGetPixelFormatType(picture) != N2_PIXFMT_420V ||
	    CVPixelBufferLockBaseAddress(picture, kCVPixelBufferLock_ReadOnly) != 0) {
		return -1.0;
	}
	base = CVPixelBufferGetBaseAddressOfPlane(picture, 0);
	stride = CVPixelBufferGetBytesPerRowOfPlane(picture, 0);
	if (base == NULL) {
		CVPixelBufferUnlockBaseAddress(picture, kCVPixelBufferLock_ReadOnly);
		return -1.0;
	}
	for (size_t y = 0; y < height; y++) {
		for (size_t x = 0; x < width; x++) {
			sum += base[y * stride + x];
		}
	}
	CVPixelBufferUnlockBaseAddress(picture, kCVPixelBufferLock_ReadOnly);
	return (double)sum / (double)(width * height);
}

static id
reader_track_output(id track, id settings)
{
	id output_class = (id)objc_getClass("AVAssetReaderTrackOutput");
	id output;

	if (output_class == NULL || track == NULL) {
		return NULL;
	}
	output = ((id (*)(id, SEL))objc_msgSend)(output_class, N2O_SEL("alloc"));
	if (output == NULL) {
		return NULL;
	}
	return ((id (*)(id, SEL, id, id))objc_msgSend)(output, N2O_SEL("initWithTrack:outputSettings:"), track, settings);
}

/* AVAsset has tracksWithMediaType: (NSArray), not a singular trackWithMediaType: in iOS 16's AVFCore. */
static id
first_track(id asset, id media_type)
{
	id array;
	unsigned long count;

	if (asset == NULL || media_type == NULL) {
		return NULL;
	}
	array = ((id (*)(id, SEL, id))objc_msgSend)(asset, N2O_SEL("tracksWithMediaType:"), media_type);
	if (array == NULL) {
		return NULL;
	}
	count = ((unsigned long (*)(id, SEL))objc_msgSend)(array, N2O_SEL("count"));
	if (count == 0) {
		return NULL;
	}
	return ((id (*)(id, SEL, unsigned long))objc_msgSend)(array, N2O_SEL("objectAtIndex:"), 0UL);
}

static int
read_movie(struct probe_ctx *c, const char *prefix, const char *path, int expect_frames, int require_audio)
{
	void *pool;
	id media_video, media_audio, url, asset = NULL, tracks = NULL, video_track = NULL, audio_track = NULL;
	id reader = NULL, video_output = NULL, audio_output = NULL, error = NULL;
	id video_keys[1], video_values[1], video_settings;
	id audio_keys[7], audio_values[7], audio_settings;
	id asset_class, reader_class;
	N2O_SIZE natural;
	CMTime duration;
	long track_count = 0;
	long frames = 0;
	long samples = 0;
	long reader_status = -1;
	double luma = -1.0;
	char error_buffer[512];
	int result = 1;

	pool = objc_autoreleasePoolPush();
	if (load_frameworks(c, prefix) != 0) {
		objc_autoreleasePoolPop(pool);
		return 1;
	}
	media_video = constant_id(c, prefix, "AVMediaTypeVideo", "vide");
	media_audio = constant_id(c, prefix, "AVMediaTypeAudio", "soun");
	asset_class = (id)objc_getClass("AVURLAsset");
	reader_class = (id)objc_getClass("AVAssetReader");
	url = nsurl_path(path);
	if (media_video == NULL) {
		sayf(c, prefix, "const.media.video", "0");
		goto out;
	}
	if (media_audio == NULL) {
		sayf(c, prefix, "const.media.audio", "0");
		goto out;
	}
	if (asset_class == NULL || reader_class == NULL) {
		sayf(c, prefix, "class.reader", "%d", asset_class != NULL && reader_class != NULL);
		goto out;
	}
	if (url == NULL) {
		sayf(c, prefix, "url", "0");
		goto out;
	}
	asset = ((id (*)(id, SEL, id))objc_msgSend)(asset_class, N2O_SEL("assetWithURL:"), url);
	if (asset == NULL) {
		sayf(c, prefix, "asset", "0");
		goto out;
	}
	sayf(c, prefix, "asset", "1");
	tracks = ((id (*)(id, SEL))objc_msgSend)(asset, N2O_SEL("tracks"));
	track_count = tracks != NULL ? ((unsigned long (*)(id, SEL))objc_msgSend)(tracks, N2O_SEL("count")) : 0;
	sayf(c, prefix, "tracks", "%ld", track_count);
	video_track = first_track(asset, media_video);
	if (video_track == NULL) {
		sayf(c, prefix, "track.video", "0");
		goto out;
	}
	natural = n2o_size(video_track, "naturalSize");
	duration = n2o_cmtime(asset, "duration");
	sayf(c, prefix, "natural", "%.0fx%.0f", natural.width, natural.height);
	sayf(c, prefix, "duration", "%.3f", duration.timescale != 0 ? (double)duration.value / (double)duration.timescale : 0.0);
	sayf(c, prefix, "fps", "%.2f", (double)n2o_float(video_track, "nominalFrameRate"));

	reader = ((id (*)(id, SEL, id, id *))objc_msgSend)(reader_class, N2O_SEL("assetReaderWithAsset:error:"), asset, &error);
	if (reader == NULL) {
		error_text(error, error_buffer, sizeof(error_buffer));
		sayf(c, prefix, "reader", "0");
		sayf(c, prefix, "error", "%s", error_buffer);
		goto out;
	}
	video_keys[0] = constant_id(c, prefix, "kCVPixelBufferPixelFormatTypeKey", "PixelFormatType");
	video_values[0] = nsnum((int)N2_PIXFMT_420V);
	if (any_null(video_keys, 1) || any_null(video_values, 1)) {
		sayf(c, prefix, "video.values", "0");
		goto out;
	}
	video_settings = make_dict(video_values, video_keys, 1);
	video_output = reader_track_output(video_track, video_settings);
	if (video_output == NULL) {
		sayf(c, prefix, "output.video", "0");
		goto out;
	}
	sayf(c, prefix, "output.video", "1");

	audio_track = first_track(asset, media_audio);
	if (audio_track != NULL) {
		audio_keys[0] = constant_id(c, prefix, "AVFormatIDKey", "AVFormatIDKey");
		audio_keys[1] = constant_id(c, prefix, "AVSampleRateKey", "AVSampleRateKey");
		audio_keys[2] = constant_id(c, prefix, "AVNumberOfChannelsKey", "AVNumberOfChannelsKey");
		audio_keys[3] = constant_id(c, prefix, "AVLinearPCMBitDepthKey", "AVLinearPCMBitDepthKey");
		audio_keys[4] = constant_id(c, prefix, "AVLinearPCMIsFloatKey", "AVLinearPCMIsFloatKey");
		audio_keys[5] = constant_id(c, prefix, "AVLinearPCMIsBigEndianKey", "AVLinearPCMIsBigEndianKey");
		audio_keys[6] = constant_id(c, prefix, "AVLinearPCMIsNonInterleaved", "AVLinearPCMIsNonInterleaved");
		audio_values[0] = nsnum(1819304813);      /* 'lpcm' */
		audio_values[1] = nsnum(44100);
		audio_values[2] = nsnum(1);
		audio_values[3] = nsnum(16);
		audio_values[4] = nsbool(0);
		audio_values[5] = nsbool(0);
		audio_values[6] = nsbool(0);
		if (any_null(audio_keys, 7) || any_null(audio_values, 7)) {
			sayf(c, prefix, "audio.values", "0");
			goto out;
		}
		audio_settings = make_dict(audio_values, audio_keys, 7);
		audio_output = reader_track_output(audio_track, audio_settings);
		sayf(c, prefix, "output.audio", audio_output != NULL ? "1" : "0");
	} else {
		sayf(c, prefix, "output.audio", "absent");
	}

	if (!((BOOL (*)(id, SEL, id))objc_msgSend)(reader, N2O_SEL("canAddOutput:"), video_output)) {
		sayf(c, prefix, "canAdd.video", "0");
		goto out;
	}
	((void (*)(id, SEL, id))objc_msgSend)(reader, N2O_SEL("addOutput:"), video_output);
	if (audio_output != NULL) {
		((void (*)(id, SEL, id))objc_msgSend)(reader, N2O_SEL("addOutput:"), audio_output);
	}
	if (!((BOOL (*)(id, SEL))objc_msgSend)(reader, N2O_SEL("startReading"))) {
		error = ((id (*)(id, SEL))objc_msgSend)(reader, N2O_SEL("error"));
		error_text(error, error_buffer, sizeof(error_buffer));
		sayf(c, prefix, "startReading", "0");
		sayf(c, prefix, "error", "%s", error_buffer);
		goto out;
	}
	sayf(c, prefix, "startReading", "1");

	for (unsigned guard = 0; guard < 1000; guard++) {
		CMSampleBufferRef sample = ((CMSampleBufferRef (*)(id, SEL))objc_msgSend)(
			video_output, N2O_SEL("copyNextSampleBuffer"));

		if (sample == NULL) {
			break;
		}
		if (frames == 0) {
			CVImageBufferRef picture = CMSampleBufferGetImageBuffer(sample);

			if (picture != NULL) {
				luma = mean_luma(picture);
			}
		}
		frames++;
		CFRelease(sample);
	}
	sayf(c, prefix, "frames", "%ld", frames);
	sayf(c, prefix, "luma", "%.2f", luma);

	if (audio_output != NULL) {
		for (unsigned guard = 0; guard < 10000; guard++) {
			CMSampleBufferRef sample = ((CMSampleBufferRef (*)(id, SEL))objc_msgSend)(
				audio_output, N2O_SEL("copyNextSampleBuffer"));

			if (sample == NULL) {
				break;
			}
			samples += CMSampleBufferGetNumSamples(sample);
			CFRelease(sample);
		}
	}
	sayf(c, prefix, "audio.samples", "%ld", samples);
	reader_status = ((long (*)(id, SEL))objc_msgSend)(reader, N2O_SEL("status"));
	sayf(c, prefix, "reader.status", "%ld", reader_status);
	if (reader_status != 2) {
		error = ((id (*)(id, SEL))objc_msgSend)(reader, N2O_SEL("error"));
		error_text(error, error_buffer, sizeof(error_buffer));
		sayf(c, prefix, "error", "%s", error_buffer);
	}

	if ((expect_frames > 0 ? frames == expect_frames : frames > 0) && luma >= 0.0 &&
	    (!require_audio || samples > 0)) {
		result = 0;
	}

out:
	objc_autoreleasePoolPop(pool);
	return result;
}

static int
t11_read(struct probe_ctx *c)
{
	char path[512];

	movie_path(path, sizeof(path), "t10.mp4");
	return read_movie(c, "t11", path, 60, 1);
}

/* (lead) the HEVC movie T10b wrote, read back the same way (T11 depends on the H.264 file) */
static int
t11b_read(struct probe_ctx *c)
{
	char path[512];

	movie_path(path, sizeof(path), "t10b.mov");
	return read_movie(c, "t11b", path, 60, 1);
}

/* ---- T12/T12a: a headless AVCaptureSession + AVCaptureMovieFileOutput recording ---------------------------- */

static dispatch_semaphore_t g_capture_semaphore;
static volatile long g_capture_done;
static volatile long g_capture_ok;
static char g_capture_error[512];
static char g_capture_url[512];

static void
t12_delegate_finished(id self, SEL _cmd, id output, id file_url, id connections, id error)
{
	const char *text;

	(void)self;
	(void)_cmd;
	(void)output;
	(void)connections;
	g_capture_done = 1;
	g_capture_ok = error == NULL;
	g_capture_error[0] = 0;
	if (error != NULL) {
		text = n2o_utf8(((id (*)(id, SEL))objc_msgSend)(error, N2O_SEL("description")));
		if (text != NULL) {
			snprintf(g_capture_error, sizeof(g_capture_error), "%s", text);
		}
	}
	g_capture_url[0] = 0;
	if (file_url != NULL) {
		text = n2o_utf8(((id (*)(id, SEL))objc_msgSend)(file_url, N2O_SEL("path")));
		if (text != NULL) {
			snprintf(g_capture_url, sizeof(g_capture_url), "%s", text);
		}
	}
	if (g_capture_semaphore != NULL) {
		dispatch_semaphore_signal(g_capture_semaphore);
	}
}

static Class
t12_delegate_class(struct probe_ctx *c)
{
	Class existing = objc_getClass("N2ProbeMovieDelegate");
	Class cls;
	BOOL added;

	if (existing != NULL) {
		return existing;
	}
	cls = objc_allocateClassPair(objc_getClass("NSObject"), "N2ProbeMovieDelegate", 0);
	if (cls == NULL) {
		sayf(c, "t12", "class", "alloc:0");
		return NULL;
	}
	added = class_addMethod(cls, N2O_SEL("captureOutput:didFinishRecordingToOutputFileAtURL:fromConnections:error:"),
	                        (IMP)(void *)&t12_delegate_finished, "v48@0:8@16@24@32@40");
	objc_registerClassPair(cls);
	sayf(c, "t12", "class", "alloc:1,method:%d", (int)added);
	return added ? cls : NULL;
}

static int
capture_movie(struct probe_ctx *c, int with_audio)
{
	const char *prefix = with_audio ? "t12a" : "t12";
	char switch_path[512];
	char path[512];
	void *pool;
	id media_video, media_audio, preset, device, session, device_input = NULL, movie_output;
	id audio_device = NULL, audio_input = NULL;
	id url, delegate, error = NULL;
	id device_class, input_class, session_class, movie_class;
	long authorization;
	long finish_woken = 0;
	long bytes;
	int preset_ok = 0;
	int result = 1;

	snprintf(switch_path, sizeof(switch_path), "%s/nord2-video-probe-camera", database_dir());
	if (access(switch_path, F_OK) != 0) {
		sayf(c, prefix, "switch", "absent:%s", switch_path);
		return 2;
	}
	g_capture_done = 0;
	g_capture_ok = 0;
	g_capture_error[0] = 0;
	g_capture_url[0] = 0;
	g_capture_semaphore = NULL;
	pool = objc_autoreleasePoolPush();
	sayf(c, prefix, "switch", "present");
	if (load_frameworks(c, prefix) != 0) {
		objc_autoreleasePoolPop(pool);
		return 1;
	}
	media_video = constant_id(c, prefix, "AVMediaTypeVideo", "vide");
	media_audio = constant_id(c, prefix, "AVMediaTypeAudio", "soun");
	device_class = (id)objc_getClass("AVCaptureDevice");
	input_class = (id)objc_getClass("AVCaptureDeviceInput");
	session_class = (id)objc_getClass("AVCaptureSession");
	movie_class = (id)objc_getClass("AVCaptureMovieFileOutput");
	if (media_video == NULL) {
		sayf(c, prefix, "const.media.video", "0");
		goto out;
	}
	if (media_audio == NULL) {
		sayf(c, prefix, "const.media.audio", "0");
		goto out;
	}
	if (device_class == NULL || input_class == NULL || session_class == NULL || movie_class == NULL) {
		sayf(c, prefix, "class.capture", "0");
		goto out;
	}
	authorization = ((long (*)(id, SEL, id))objc_msgSend)(
		device_class, N2O_SEL("authorizationStatusForMediaType:"), media_video);
	sayf(c, prefix, "auth", "%ld", authorization);

	device = ((id (*)(id, SEL, id))objc_msgSend)(
		device_class, N2O_SEL("defaultDeviceWithMediaType:"), media_video);
	if (device == NULL) {
		sayf(c, prefix, "device", "0");
		goto out;
	}
	sayf(c, prefix, "device", "1");
	session = new_object("AVCaptureSession");
	if (session == NULL) {
		sayf(c, prefix, "session", "0");
		goto out;
	}
	sayf(c, prefix, "session", "1");

	preset = constant_id(c, prefix, "AVCaptureSessionPresetHigh", "AVCaptureSessionPresetHigh");
	if (preset != NULL && ((BOOL (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("canSetSessionPreset:"), preset)) {
		((void (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("setSessionPreset:"), preset);
		preset_ok = 1;
	} else {
		id fallback = constant_id(c, prefix, "AVCaptureSessionPreset1280x720", "AVCaptureSessionPreset1280x720");

		if (fallback != NULL && ((BOOL (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("canSetSessionPreset:"), fallback)) {
			((void (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("setSessionPreset:"), fallback);
			preset = fallback;
			preset_ok = 1;
		}
	}
	{
		const char *preset_text = preset != NULL ? n2o_utf8(preset) : NULL;

		sayf(c, prefix, "preset", "%s:%d", preset_text != NULL ? preset_text : "nil", preset_ok);
	}
	if (!preset_ok) {
		goto out;
	}

	error = NULL;
	device_input = ((id (*)(id, SEL, id, id *))objc_msgSend)(
		input_class, N2O_SEL("deviceInputWithDevice:error:"), device, &error);
	if (device_input == NULL) {
		char text[512];

		error_text(error, text, sizeof(text));
		sayf(c, prefix, "input.video", "0");
		sayf(c, prefix, "error", "%s", text);
		goto out;
	}
	sayf(c, prefix, "input.video", "1");
	if (!((BOOL (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("canAddInput:"), device_input)) {
		sayf(c, prefix, "canAdd.input", "0");
		goto out;
	}
	((void (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("addInput:"), device_input);
	sayf(c, prefix, "add.input", "1");

	if (with_audio) {
		audio_device = ((id (*)(id, SEL, id))objc_msgSend)(
			device_class, N2O_SEL("defaultDeviceWithMediaType:"), media_audio);
		if (audio_device != NULL) {
			error = NULL;
			audio_input = ((id (*)(id, SEL, id, id *))objc_msgSend)(
				input_class, N2O_SEL("deviceInputWithDevice:error:"), audio_device, &error);
		}
		if (audio_input == NULL ||
		    !((BOOL (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("canAddInput:"), audio_input)) {
			sayf(c, prefix, "audio", "failed");
			audio_input = NULL;
		} else {
			((void (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("addInput:"), audio_input);
			sayf(c, prefix, "audio", "on");
		}
	} else {
		sayf(c, prefix, "audio", "off");
	}

	movie_output = new_object("AVCaptureMovieFileOutput");
	if (movie_output == NULL) {
		sayf(c, prefix, "output", "0");
		goto out;
	}
	if (!((BOOL (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("canAddOutput:"), movie_output)) {
		sayf(c, prefix, "canAdd.output", "0");
		goto out;
	}
	((void (*)(id, SEL, id))objc_msgSend)(session, N2O_SEL("addOutput:"), movie_output);
	sayf(c, prefix, "add.output", "1");
#ifndef N2_HOST_FAKE
	{       /* (lead) which codecs the movie output offers on this phone, and H.264 for this recording: the output's own
		 * default is the first of the list (HEVC when present, and Apple's software HEVC encoder is ~1 s per 1080p picture) */
		id connection = ((id (*)(id, SEL, id))objc_msgSend)(movie_output, N2O_SEL("connectionWithMediaType:"), media_video);
		id codecs = ((id (*)(id, SEL))objc_msgSend)(movie_output, N2O_SEL("availableVideoCodecTypes"));
		id text = codecs != NULL ? ((id (*)(id, SEL, id))objc_msgSend)(codecs, N2O_SEL("componentsJoinedByString:"), nsstr(",")) : NULL;
		id codec_key = constant_id(c, prefix, "AVVideoCodecKey", "AVVideoCodecKey");
		const char *t = text != NULL ? n2o_utf8(text) : NULL;

		sayf(c, prefix, "connection.video", "%d", connection != NULL);
		sayf(c, prefix, "codecs", "%s", t != NULL ? t : "nil");
		if (connection != NULL && codec_key != NULL && access("/private/var/db/nord2-video-probe-camera-hevc", 0) != 0) {
			id objects[1] = { nsstr("avc1") }, keys[1] = { codec_key };
			id settings = make_dict(objects, keys, 1);
			if (settings != NULL) {
				((void (*)(id, SEL, id, id))objc_msgSend)(movie_output, N2O_SEL("setOutputSettings:forConnection:"), settings, connection);
				sayf(c, prefix, "codec.set", "avc1");
			}
		}
	}
#endif

	((void (*)(id, SEL))objc_msgSend)(session, N2O_SEL("startRunning"));
	sayf(c, prefix, "startRunning", "1");
	usleep(2 * 1000 * 1000);

	snprintf(path, sizeof(path), "%s/nord2-video/%s.mov", database_dir(), with_audio ? "t12a" : "t12");
	unlink(path);
	url = nsurl_path(path);
	delegate = NULL;
	{
		Class delegate_class = t12_delegate_class(c);

		if (delegate_class != NULL) {
			delegate = new_object("N2ProbeMovieDelegate");
		}
	}
	if (url == NULL || delegate == NULL) {
		sayf(c, prefix, "record.start", "0");
		goto stop;
	}
	g_capture_done = 0;
	g_capture_ok = 0;
	g_capture_error[0] = 0;
	g_capture_url[0] = 0;
	g_capture_semaphore = dispatch_semaphore_create(0);
	if (g_capture_semaphore == NULL) {
		sayf(c, prefix, "record.start", "0");
		goto stop;
	}
	((void (*)(id, SEL, id, id))objc_msgSend)(movie_output, N2O_SEL("startRecordingToOutputFileURL:recordingDelegate:"),
	                                          url, delegate);
	sayf(c, prefix, "record.start", "1");
	usleep(3 * 1000 * 1000);
	((void (*)(id, SEL))objc_msgSend)(movie_output, N2O_SEL("stopRecording"));
	sayf(c, prefix, "record.stop", "1");
	finish_woken = dispatch_semaphore_wait(g_capture_semaphore, dispatch_time(N2O_TIME_NOW, N2O_TIME_SECONDS(10))) == 0;
	sayf(c, prefix, "delegate", "%ld", finish_woken);
	sayf(c, prefix, "delegate.done", "%ld", g_capture_done);
	sayf(c, prefix, "delegate.ok", "%ld", g_capture_ok);
	sayf(c, prefix, "delegate.url", "%s", g_capture_url);
	sayf(c, prefix, "error", "%s", g_capture_error);

stop:
	((void (*)(id, SEL))objc_msgSend)(session, N2O_SEL("stopRunning"));
	bytes = file_size(path);
	sayf(c, prefix, "bytes", "%ld", bytes);
	if (finish_woken == 0 && g_capture_done == 0) {
		/* no delegate call: the reader below still says whether anything was written */
	}
	if (finish_woken && g_capture_done && g_capture_ok && bytes > 0) {
		char read_prefix[32];

		snprintf(read_prefix, sizeof(read_prefix), "%s.read", prefix);
		if (read_movie(c, read_prefix, path, 0, with_audio) == 0) {
			result = 0;
		}
	}

out:
	objc_autoreleasePoolPop(pool);
	return result;
}

static int
t12_capture_movie(struct probe_ctx *c)
{
	return capture_movie(c, 0);
}

static int
t12a_capture_movie_audio(struct probe_ctx *c)
{
	return capture_movie(c, 1);
}

/* ---- the table the probe runner executes ------------------------------------------------------------------- */

const struct probe_test probe_av_tests[] = {
	{ "av.write.h264.aac", t10_write_h264_aac, 60 },
	{ "av.write.h265", t10b_write_hevc, 120 },
	{ "av.read", t11_read, 40 },
	{ "av.capture.movie", t12_capture_movie, 30 },
	{ "av.capture.movie.audio", t12a_capture_movie_audio, 30 },
	{ "av.read.h265", t11b_read, 60 },      /* (lead) last: the host oracle indexes the entries above */
};

const unsigned probe_av_test_count = sizeof(probe_av_tests) / sizeof(probe_av_tests[0]);
