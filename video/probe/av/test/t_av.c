/* SPDX-License-Identifier: MIT */
/*
 * t_av.c -- host oracle for probe/av/probe_av.c (WP06). It links the probe against fake_objc.c, a made-up
 * Objective-C runtime, and checks what the probe does: the exact selector sequence of T10, that a scripted
 * "startWriting = NO" stops T10 before any append and reports the error, the literal settings keys and numbers,
 * the T11 read-back path, and that T12 sends nothing when its switch file is absent (result 2).
 *
 * It is NOT a test of AVFoundation. The expected values are the task's own literal requirements (640x480, 30 fps,
 * 1 Mbit/s, 44100 Hz mono, 64 kbit/s, 60 frames, the two file types) and the selectors named in WP06's TASK.md.
 *
 * Built by run_tests.sh with ASan+UBSan; no file below the build directory is touched.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "probe_objc.h"
#include "probe_core.h"
#include "fake_objc.h"

extern const struct probe_test probe_av_tests[];
extern const unsigned probe_av_test_count;

/* probe_core.h only forward-declares this: the probe does not look inside it. */
struct probe_ctx {
	int unused;
};

#define SAY_MAX 800
struct say_entry {
	char key[128];
	char value[512];
};
static struct say_entry says[SAY_MAX];
static int say_count;

void
probe_say(struct probe_ctx *c, const char *key, const char *format, ...)
{
	va_list ap;

	(void)c;
	if (say_count < SAY_MAX) {
		snprintf(says[say_count].key, sizeof(says[say_count].key), "%s", key);
		va_start(ap, format);
		vsnprintf(says[say_count].value, sizeof(says[say_count].value), format, ap);
		va_end(ap);
		if (getenv("VERBOSE") != NULL) {
			printf("   | %s=%s\n", says[say_count].key, says[say_count].value);
		}
		say_count++;
	}
}

/* probe_core.c's deterministic picture, from its frozen description (probe_core.h). */
void
probe_fill_nv12(uint8_t *y, size_t ystride, uint8_t *uv, size_t uvstride, int w, int h, int frame)
{
	for (int row = 0; row < h; row++) {
		for (int column = 0; column < w; column++) {
			y[row * (int)ystride + column] = (uint8_t)((column + 2 * row + 3 * frame) & 255);
		}
	}
	for (int row = 0; row < 32; row++) {
		for (int column = 0; column < 32; column++) {
			int x = 16 + 4 * frame + column;
			int yy = 16 + 2 * frame + row;

			if (x < w && yy < h) {
				y[yy * (int)ystride + x] = 235;
			}
		}
	}
	for (int row = 0; row < h / 2; row++) {
		for (int column = 0; column < w / 2; column++) {
			uv[row * (int)uvstride + 2 * column] = (uint8_t)(128 + ((column + frame) & 31));
			uv[row * (int)uvstride + 2 * column + 1] = (uint8_t)(128 - (row & 31));
		}
	}
}

static int checks;
static int failures;

#define CHECK(condition)                                                                                       \
	do {                                                                                                   \
		checks++;                                                                                      \
		if (!(condition)) {                                                                            \
			failures++;                                                                            \
			printf("FAIL line %d: %s\n", __LINE__, #condition);                                    \
		}                                                                                              \
	} while (0)

static void
reset_says(void)
{
	say_count = 0;
}

static const char *
said(const char *key)
{
	for (int i = say_count - 1; i >= 0; i--) {
		if (strcmp(says[i].key, key) == 0) {
			return says[i].value;
		}
	}
	return NULL;
}

static long
said_long(const char *key)
{
	const char *value = said(key);

	return value != NULL ? atol(value) : -99999;
}

static int
said_contains(const char *key, const char *text)
{
	const char *value = said(key);

	return value != NULL && strstr(value, text) != NULL;
}

static int
has_fallback_key(void)
{
	for (int i = 0; i < say_count; i++) {
		if (strstr(says[i].key, ".const.") != NULL) {
			return 1;
		}
	}
	return 0;
}

static int
class_requested(const char *name)
{
	for (int i = 0; i < fake_objc_class_request_count(); i++) {
		if (strcmp(fake_objc_class_request_at(i), name) == 0) {
			return 1;
		}
	}
	return 0;
}

static int
symbol_requested(const char *name)
{
	for (int i = 0; i < fake_objc_symbol_count(); i++) {
		if (strcmp(fake_objc_symbol_at(i), name) == 0) {
			return 1;
		}
	}
	return 0;
}

static int
is_writer_class(const char *name)
{
	return strncmp(name, "AVAssetWriter", 13) == 0;
}

static const char *
next_writer_selector(int *index)
{
	while (*index < fake_objc_count()) {
		const char *cls = fake_objc_class_at(*index);
		const char *selector = fake_objc_selector_at(*index);

		(*index)++;
		if (!is_writer_class(cls)) {
			continue;
		}
		if (strcmp(selector, "isReadyForMoreMediaData") == 0 || strcmp(selector, "pixelBufferPool") == 0) {
			continue;
		}
		return selector;
	}
	return NULL;
}

#define R7(x) x, x, x, x, x, x, x
#define R10(x) x, x, x, x, x, x, x, x, x, x

static void
check_writer_sequence(void)
{
	static const char *const expected[] = {
		"assetWriterWithURL:fileType:error:",
		"assetWriterInputWithMediaType:outputSettings:",
		"setExpectsMediaDataInRealTime:",
		"assetWriterInputWithMediaType:outputSettings:",
		"setExpectsMediaDataInRealTime:",
		"assetWriterInputPixelBufferAdaptorWithAssetWriterInput:sourcePixelBufferAttributes:",
		"canAddInput:",
		"addInput:",
		"canAddInput:",
		"addInput:",
		"startWriting",
		"startSessionAtSourceTime:",   /* (lead) required before any append */
		R10("appendPixelBuffer:withPresentationTime:"), R10("appendPixelBuffer:withPresentationTime:"),
		R10("appendPixelBuffer:withPresentationTime:"), R10("appendPixelBuffer:withPresentationTime:"),
		R10("appendPixelBuffer:withPresentationTime:"), R10("appendPixelBuffer:withPresentationTime:"),
		R10("appendSampleBuffer:"), R10("appendSampleBuffer:"), R10("appendSampleBuffer:"),
		R10("appendSampleBuffer:"), R10("appendSampleBuffer:"), R10("appendSampleBuffer:"),
		R10("appendSampleBuffer:"), R10("appendSampleBuffer:"), R7("appendSampleBuffer:"),
		"markAsFinished",
		"markAsFinished",
		"finishWritingWithCompletionHandler:",
		"status",
		"error",
	};
	int index = 0;
	int ok = 1;

	for (size_t e = 0; e < sizeof(expected) / sizeof(expected[0]); e++) {
		const char *selector = next_writer_selector(&index);

		if (selector == NULL || strcmp(selector, expected[e]) != 0) {
			printf("FAIL selector %zu: got %s, expected %s\n", e, selector != NULL ? selector : "(end)",
			       expected[e]);
			ok = 0;
			break;
		}
	}
	if (ok && next_writer_selector(&index) != NULL) {
		printf("FAIL extra selector after the writer sequence\n");
		ok = 0;
	}
	CHECK(ok);
}

static void
check_video_settings(void)
{
	id settings = fake_find_dictionary("AVVideoCodecKey");
	id compression;
	id attrs;

	CHECK(settings != NULL);
	if (settings == NULL) {
		return;
	}
	CHECK(strcmp(fake_text(fake_dict_get(settings, "AVVideoCodecKey")), "avc1") == 0);
	CHECK(fake_number(fake_dict_get(settings, "AVVideoWidthKey")) == 640);
	CHECK(fake_number(fake_dict_get(settings, "AVVideoHeightKey")) == 480);
	compression = fake_dict_get(settings, "AVVideoCompressionPropertiesKey");
	CHECK(compression != NULL);
	CHECK(fake_number(fake_dict_get(compression, "AVVideoAverageBitRateKey")) == 1000000);
	attrs = fake_find_dictionary("PixelFormatType");
	CHECK(attrs != NULL);
	CHECK(fake_number(fake_dict_get(attrs, "PixelFormatType")) == 875704438);      /* '420v' */
	CHECK(fake_number(fake_dict_get(attrs, "Width")) == 640);
	CHECK(fake_number(fake_dict_get(attrs, "Height")) == 480);
}

static void
check_audio_settings(void)
{
	id settings = fake_find_dictionary("AVFormatIDKey");

	CHECK(settings != NULL);
	if (settings == NULL) {
		return;
	}
	CHECK(fake_number(fake_dict_get(settings, "AVFormatIDKey")) == 1633772320);    /* 'aac ' */
	CHECK(fake_number(fake_dict_get(settings, "AVSampleRateKey")) == 44100);
	CHECK(fake_number(fake_dict_get(settings, "AVNumberOfChannelsKey")) == 1);
	CHECK(fake_number(fake_dict_get(settings, "AVEncoderBitRateKey")) == 64000);
}

/* ---- the tests --------------------------------------------------------------------------------------------- */

static void
test_t10_writes(void)
{
	static struct probe_ctx ctx;
	id video_input;
	id audio_input;
	int result;

	fake_objc_reset();
	reset_says();
	result = probe_av_tests[0].fn(&ctx);
	CHECK(result == 0);
	CHECK(said_long("t10.startWriting") == 1);
	CHECK(said_long("t10.append.video") == 60);
	CHECK(said_long("t10.audio.buffers") == 87);
	CHECK(said_long("t10.audio.samples") == 88200);
	CHECK(said_long("t10.status") == 2);
	CHECK(said_long("t10.bytes") > 0);
	CHECK(said_long("t10.finish") == 1);
	CHECK(fake_stats.append_pixel_calls == 60);
	CHECK(fake_stats.append_audio_calls == 87);
	CHECK(fake_stats.audio_samples == 88200);
	CHECK(fake_stats.mark_finished_calls == 2);
	CHECK(fake_stats.finish_calls == 1);
	CHECK(fake_stats.append_pixel_last_value == 59);
	CHECK(fake_stats.append_pixel_last_timescale == 30);
	CHECK(strstr(fake_writer_url(), "t10.mp4") != NULL);
	CHECK(strcmp(fake_writer_file_type(), "public.mpeg-4") == 0);
	video_input = fake_find_class_object("AVAssetWriterInput", 0);
	audio_input = fake_find_class_object("AVAssetWriterInput", 1);
	CHECK(video_input != NULL && audio_input != NULL);
	if (video_input != NULL && audio_input != NULL) {
		CHECK(fake_realtime(video_input) == 1);
		CHECK(fake_realtime(audio_input) == 1);
		CHECK(strcmp(fake_text(video_input), "vide") == 0);
		CHECK(strcmp(fake_text(audio_input), "soun") == 0);
	}
	check_video_settings();
	check_audio_settings();
	check_writer_sequence();
	CHECK(!has_fallback_key());
}

static void
test_t10_start_failure(void)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	fake_script.start_writing = 0;
	fake_script.writer_error = "scripted start failure";
	result = probe_av_tests[0].fn(&ctx);
	CHECK(result == 1);
	CHECK(said_long("t10.startWriting") == 0);
	CHECK(said_contains("t10.error", "scripted start failure"));
	CHECK(fake_stats.append_pixel_calls == 0);
	CHECK(!fake_objc_has_selector("appendPixelBuffer:withPresentationTime:"));
	CHECK(!fake_objc_has_selector("appendSampleBuffer:"));
}

static void
test_t10b_writes_hevc(void)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	result = probe_av_tests[1].fn(&ctx);
	CHECK(result == 0);
	CHECK(said_long("t10b.append.video") == 60);
	CHECK(said_long("t10b.status") == 2);
	CHECK(strcmp(fake_writer_file_type(), "com.apple.quicktime-movie") == 0);
	CHECK(symbol_requested("AVFileTypeQuickTimeMovie"));
	CHECK(strstr(fake_writer_url(), "t10b.mov") != NULL);
	CHECK(strcmp(fake_text(fake_dict_get(fake_find_dictionary("AVVideoCodecKey"), "AVVideoCodecKey")), "hvc1") == 0);
}

static void
test_t11_reads(void)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	fake_script.reader_frames = 60;
	fake_script.reader_audio_samples = 88200;
	result = probe_av_tests[2].fn(&ctx);
	CHECK(result == 0);
	CHECK(said_long("t11.frames") == 60);
	CHECK(said_long("t11.audio.samples") == 88200);
	CHECK(said_long("t11.tracks") == 2);
	CHECK(strcmp(said("t11.natural") != NULL ? said("t11.natural") : "", "640x480") == 0);
	CHECK(said_long("t11.reader.status") == 2);
	CHECK(said("t11.luma") != NULL && atof(said("t11.luma")) == 128.0);
}

static void
test_constants_and_frameworks(void)
{
	static struct probe_ctx ctx;
	static const char *const symbols[] = {
		"AVFileTypeMPEG4", "AVMediaTypeVideo", "AVMediaTypeAudio",
		"AVVideoCodecKey", "AVVideoWidthKey", "AVVideoHeightKey", "AVVideoCompressionPropertiesKey",
		"AVVideoAverageBitRateKey", "AVFormatIDKey", "AVSampleRateKey", "AVNumberOfChannelsKey",
		"AVEncoderBitRateKey", "kCVPixelBufferPixelFormatTypeKey", "kCVPixelBufferWidthKey",
		"kCVPixelBufferHeightKey",
	};
	static const char *const frameworks[] = {
		"/System/Library/Frameworks/AVFoundation.framework/AVFoundation",
		"/System/Library/Frameworks/CoreMedia.framework/CoreMedia",
		"/System/Library/Frameworks/AudioToolbox.framework/AudioToolbox",
	};

	fake_objc_reset();
	reset_says();
	(void)probe_av_tests[0].fn(&ctx);
	for (size_t i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
		CHECK(symbol_requested(symbols[i]));
	}
	CHECK(fake_stats.dlsym_misses == 0);
	CHECK(!has_fallback_key());
	CHECK(fake_objc_framework_count() == 3);
	for (size_t i = 0; i < sizeof(frameworks) / sizeof(frameworks[0]); i++) {
		CHECK(strcmp(fake_objc_framework_at((int)i), frameworks[i]) == 0);
	}
	CHECK(class_requested("AVAssetWriter"));
	CHECK(class_requested("AVAssetWriterInput"));
	CHECK(class_requested("AVAssetWriterInputPixelBufferAdaptor"));
}

static void
test_t12_skips_without_switch(const char *switch_path)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	unlink(switch_path);
	result = probe_av_tests[3].fn(&ctx);
	CHECK(result == 2);
	CHECK(fake_objc_count() == 0);
	CHECK(said("t12.switch") != NULL && strncmp(said("t12.switch"), "absent:", 7) == 0);
}

static void
test_t12a_skips_without_switch(const char *switch_path)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	unlink(switch_path);
	result = probe_av_tests[4].fn(&ctx);
	CHECK(result == 2);
	CHECK(fake_objc_count() == 0);
}

static void
make_switch(const char *switch_path)
{
	int fd = open(switch_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd >= 0) {
		close(fd);
	}
}

static void
test_t12a_records_with_audio(const char *switch_path)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	make_switch(switch_path);
	result = probe_av_tests[4].fn(&ctx);
	CHECK(result == 0);
	CHECK(strcmp(said("t12a.audio") != NULL ? said("t12a.audio") : "", "on") == 0);
	CHECK(said_long("t12a.delegate.ok") == 1);
	CHECK(said_long("t12a.read.frames") == 60);
	CHECK(said_long("t12a.read.audio.samples") == 88200);
	CHECK(fake_stats.capture_delegate_calls == 1);
}

static void
test_t12_records(const char *switch_path)
{
	static struct probe_ctx ctx;
	int result;

	fake_objc_reset();
	reset_says();
	make_switch(switch_path);
	fake_script.capture_authorization = 3;
	fake_script.capture_success = 1;
	result = probe_av_tests[3].fn(&ctx);
	CHECK(result == 0);
	CHECK(said_long("t12.auth") == 3);
	CHECK(strcmp(said("t12.audio") != NULL ? said("t12.audio") : "", "off") == 0);
	CHECK(said_long("t12.delegate.done") == 1);
	CHECK(said_long("t12.delegate.ok") == 1);
	CHECK(said_long("t12.bytes") == 1000);
	CHECK(said_long("t12.read.frames") == 60);
	CHECK(fake_stats.capture_start_calls == 1);
	CHECK(fake_stats.capture_stop_calls == 1);
	CHECK(fake_stats.capture_delegate_calls == 1);
	CHECK(fake_objc_has_selector("authorizationStatusForMediaType:"));
	CHECK(fake_objc_has_selector("startRecordingToOutputFileURL:recordingDelegate:"));
	CHECK(fake_objc_has_selector("stopRecording"));
	CHECK(fake_objc_has_selector("captureOutput:didFinishRecordingToOutputFileAtURL:fromConnections:error:"));
	CHECK(fake_objc_has_selector("setSessionPreset:"));
	CHECK(fake_objc_has_selector("addOutput:"));
	CHECK(class_requested("N2ProbeMovieDelegate"));
}

int
main(int argc, char **argv)
{
	static struct probe_ctx ctx;
	const char *base = argc > 1 ? argv[1] : "/tmp/n2wp06";
	char output_dir[512];
	char switch_path[512];

	snprintf(output_dir, sizeof(output_dir), "%s/nord2-video", base);
	mkdir(base, 0777);
	mkdir(output_dir, 0777);
	snprintf(switch_path, sizeof(switch_path), "%s/nord2-video-probe-camera", base);
	(void)ctx;

	test_t10_writes();
	test_t10_start_failure();
	test_t10b_writes_hevc();
	test_t11_reads();
	test_constants_and_frameworks();
	test_t12_skips_without_switch(switch_path);
	test_t12a_skips_without_switch(switch_path);
	test_t12_records(switch_path);
	test_t12a_records_with_audio(switch_path);
	unlink(switch_path);

	if (failures == 0) {
		printf("N2TEST WP06 PASS %d/%d\n", checks, checks);
		return 0;
	}
	printf("N2TEST WP06 FAIL %d of %d checks failed\n", failures, checks);
	return 1;
}
