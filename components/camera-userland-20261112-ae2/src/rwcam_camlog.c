/*
 * libredwoodcamlog.dylib: DIAGNOSTICS, loaded by Camera.app only (a weak load command added to a copy of its executable by
 * apps-20261104/guest-camlog.sh). Runs q10gq..q10gu: mediaserverd has the three cameras, TCC grants the Camera, an AVFoundation client gets its
 * inputs, and yet the Camera commits sessions with no source. What CameraUI / AVFCapture say about it goes to their os_log, which nothing shows
 * on this phone: this puts those lines on the console (os_log_set_hook of libsystem_trace, as rwcam_diag.c does in mediaserverd).
 */
#include "rwcam_abi.h"
#include "rwcam_log.h"

#define LOG_LINES 3000
struct log_block { void *isa; int32_t flags, reserved; void *invoke; const void *descriptor; };
static const unsigned long log_block_descriptor[2] = { 0, sizeof(struct log_block) };
static struct log_block log_block;
static struct log_block *previous;
static volatile int lines;
static char *(*log_copy)(void *);

static bool
has(const char *text, const char *word)
{
	if (text == NULL) {
		return false;
	}
	for (; *text; text++) {
		unsigned long i = 0;

		while (word[i] && text[i] == word[i]) {
			i++;
		}
		if (word[i] == 0) {
			return true;
		}
	}
	return false;
}

static void
hook(struct log_block *block, uint8_t type, void *msg)
{
	const char *format = *(const char **)((char *)msg + 0x58);
	const char *subsystem = *(const char **)((char *)msg + 0x80);
	const char *category = *(const char **)((char *)msg + 0x88);

	(void)block;
	if (lines < LOG_LINES && format != NULL &&
	    (type >= 16 || (format[0] == '<' && format[1] == '<') || has(subsystem, "amera") || has(subsystem, "coremedia") || has(subsystem, "AVF") ||
	    has(subsystem, "avfoundation") || has(subsystem, "TCC") || has(subsystem, "tcc"))) {
		char *text = log_copy(msg);

		lines++;
		if (text != NULL) {
			for (char *c = text; *c; c++) {
				if (*c == '\n') {
					*c = ' ';
				}
			}
			rwcam_say("Camera log %u [%.24s/%.16s]: %.300s", (unsigned)type, subsystem ? subsystem : "-", category ? category : "-", text);
			free(text);
		}
	}
	if (previous != NULL) {
		((void (*)(struct log_block *, uint8_t, void *))previous->invoke)(previous, type, msg);
	}
}

__attribute__((constructor))
static void
camlog_constructor(void)
{
	const char *name = getprogname();
	void *(*set_hook)(uint8_t, void *);
	void *global_block;

	if (name == NULL || strcmp(name, "Camera") != 0) {
		return;
	}
	set_hook = (void *(*)(uint8_t, void *))dlsym(RTLD_DEFAULT, "os_log_set_hook");
	global_block = dlsym(RTLD_DEFAULT, "_NSConcreteGlobalBlock");
	log_copy = (char *(*)(void *))dlsym(RTLD_DEFAULT, "os_log_copy_message_string");
	if (set_hook == NULL || global_block == NULL || log_copy == NULL) {
		rwcam_say("Camera: no os_log hook here");
		return;
	}
	log_block.isa = global_block;
	log_block.flags = 1 << 28;
	log_block.invoke = (void *)hook;
	log_block.descriptor = log_block_descriptor;
	previous = set_hook(1, &log_block);
	rwcam_say("Camera: os_log hook installed in the Camera process");
}
