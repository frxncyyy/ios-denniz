/* SPDX-License-Identifier: MIT */
/*
 * fake_objc.h -- the test-side API of the made-up Objective-C runtime in fake_objc.c. HOST TESTS ONLY.
 *
 * The fake records every message (receiver class, selector, class-vs-instance), returns scripted objects and copies
 * the settings dictionaries probe_av.c builds. It is the model of gpu/build/camera-userland-20261109/test/t_camui.c,
 * extended with the x86_64 SysV entry points objc_msgSend/objc_msgSend_stret in assembly because the probe's calls
 * return structs (CMTime, CGSize) and pass a 24-byte CMTime by value.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef N2_FAKE_OBJC_H
#define N2_FAKE_OBJC_H

#include "n2_apple.h"
#include "probe_objc.h"

struct fake_script {
	int         start_writing;          /* 0: +startWriting answers NO and -error is scripted */
	const char *writer_error;           /* the text of that -error, or NULL */
	int         writer_status;          /* AVAssetWriterStatusCompleted = 2 */
	int         append_result;          /* what appendPixelBuffer:/appendSampleBuffer: answer */
	int         finish_calls_block;     /* 0: the completion handler is never called (tests the 15 s wait) */
	int         capture_authorization;  /* AVAuthorizationStatusAuthorized = 3 */
	int         capture_success;        /* 0: the delegate gets a scripted NSError */
	long        reader_frames;          /* video sample buffers the track output yields */
	long        reader_audio_samples;   /* LPCM samples the audio track output yields */
	int         reader_status;          /* AVAssetReaderStatusCompleted = 2 */
};

struct fake_stats {
	int  start_writing_calls;
	int  append_pixel_calls;
	int  append_audio_calls;
	long audio_samples;
	int  mark_finished_calls;
	int  finish_calls;
	int  capture_start_calls;
	int  capture_stop_calls;
	int  capture_delegate_calls;
	int  record_count;
	int  class_request_count;
	int  symbol_count;
	int  framework_count;
	int  dlsym_misses;
	long append_pixel_last_value;
	int  append_pixel_last_timescale;
};

extern struct fake_script fake_script;
extern struct fake_stats  fake_stats;

void fake_objc_reset(void);
void fake_objc_clear_records(void);
int  fake_objc_count(void);
const char *fake_objc_class_at(int index);
const char *fake_objc_selector_at(int index);
int  fake_objc_is_class_at(int index);
int  fake_objc_has_selector(const char *selector);

int  fake_objc_class_request_count(void);
const char *fake_objc_class_request_at(int index);
int  fake_objc_symbol_count(void);
const char *fake_objc_symbol_at(int index);
int  fake_objc_framework_count(void);
const char *fake_objc_framework_at(int index);

id   fake_find_class_object(const char *class_name, int index);
id   fake_find_dictionary(const char *key_text);
id   fake_dict_get(id dictionary, const char *key_text);
int  fake_dict_count(id dictionary);
const char *fake_text(id object);
long fake_number(id object);
int  fake_realtime(id input);

const char *fake_writer_url(void);
const char *fake_writer_file_type(void);

#endif
