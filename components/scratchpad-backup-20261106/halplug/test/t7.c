/* host smoke test of the plug-in's object model: #includes the plug-in source */
#include "../c/audio_plugin.c"
int printf(const char *, ...);
void abort(void);
/* CF / libc stubs */
const void *__CFConstantStringClassReference[16];
const CFAllocatorRef kCFAllocatorSystemDefault;
Boolean CFEqual(const void *a, const void *b) { return a == b; }
void CFRelease(const void *a) { (void)a; }
const void *CFRetain(const void *a) { return a; }
const char *CFStringGetCStringPtr(CFStringRef s, CFStringEncoding e) { (void)s; (void)e; return "dev"; }
CFUUIDRef CFUUIDCreateFromUUIDBytes(CFAllocatorRef a, CFUUIDBytes b) { (void)a; (void)b; return 0; }

long lround(double x) { return (long)(x < 0 ? x - 0.5 : x + 0.5); }
int ioctl_calls[8]; UInt32 ioctl_last[8][3];
int ioctl(int fd, unsigned long cmd, ...) { __builtin_va_list ap; __builtin_va_start(ap, cmd); UInt32 *p = __builtin_va_arg(ap, UInt32 *); __builtin_va_end(ap);
	int n = (cmd >> 0) & 0xff; ioctl_calls[n]++; for (int i = 0; i < 3; i++) ioctl_last[n][i] = p[i < (int)((cmd >> 16) & 0x1fff) / 4 ? i : 0]; (void)fd; return 0; }
void syslog(int p, const char *f, ...) { (void)p; (void)f; }
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
static Boolean has(AudioObjectID o, UInt32 s, UInt32 sc, UInt32 el) { AudioObjectPropertyAddress a = { s, sc, el }; return has_property(NULL, o, 0, &a); }
static struct value get(AudioObjectID o, UInt32 s, UInt32 sc, UInt32 el) { AudioObjectPropertyAddress a = { s, sc, el }; return property(o, &a, 0, NULL); }
static OSStatus set32(AudioObjectID o, UInt32 s, UInt32 sc, UInt32 el, UInt32 v) { AudioObjectPropertyAddress a = { s, sc, el }; return set_property_data(NULL, o, 0, &a, 0, NULL, 4, &v); }
static OSStatus setf(AudioObjectID o, UInt32 s, Float32 v) { AudioObjectPropertyAddress a = { s, 'glob', 0 }; return set_property_data(NULL, o, 0, &a, 0, NULL, 4, &v); }
static int listed(struct value v, UInt32 x) { for (unsigned i = 0; i < v.size / 4; i++) if (v.u.words[i] == x) return 1; return 0; }
int main(void)
{
	sink = 3; memset(&lock, 0, sizeof lock);   /* glibc mutex, not the iOS initializer */
	/* the receiver port's predicate: mute on output element 3 of the codec, nothing on element 4 */
	CHECK(has(OBJECT_CODEC, 'mute', 'outp', 3) && !has(OBJECT_CODEC, 'mute', 'outp', 4) && has(OBJECT_CODEC, 'mute', 'inpt', 0));
	CHECK(!has(OBJECT_SPEAKER, 'mute', 'outp', 3));
	struct value v = get(OBJECT_CODEC, 'stm#', 'outp', 0); CHECK(v.size == 4 && v.u.words[0] == OBJECT_CODEC_OUTPUT);
	v = get(OBJECT_CODEC, 'stm#', 'inpt', 0); CHECK(v.size == 4 && v.u.words[0] == OBJECT_CODEC_INPUT);
	v = get(OBJECT_SPEAKER, 'stm#', 'outp', 0); CHECK(v.size == 4 && v.u.words[0] == OBJECT_SPEAKER_OUTPUT);
	v = get(OBJECT_CODEC, 'ownd', 'glob', 0); CHECK(listed(v, OBJECT_RECEIVER_MUTE) && listed(v, OBJECT_MIC_MUTE) && listed(v, OBJECT_RECEIVER_VOLUME) && listed(v, OBJECT_CODEC_SOURCE) && listed(v, OBJECT_CODEC_OUTPUT));
	v = get(OBJECT_CODEC, 'ctrl', 'glob', 0); CHECK(v.size == 28);
	CHECK(get(OBJECT_CODEC_OUTPUT, 'sfmt', 'glob', 0).u.format.mChannelsPerFrame == 4);
	CHECK(get(OBJECT_CODEC_OUTPUT, 'sdir', 'glob', 0).u.words[0] == 0 && get(OBJECT_CODEC_OUTPUT, 'term', 'glob', 0).u.words[0] == 'rspk');
	/* v4: the codec may be the default input device (the built-in microphone port's way into the default device),
	 * the speaker the default output device and nothing else */
	CHECK(get(OBJECT_CODEC, 'dflt', 'inpt', 0).u.words[0] == 1 && get(OBJECT_CODEC, 'dflt', 'outp', 0).u.words[0] == 0);
	CHECK(get(OBJECT_SPEAKER, 'dflt', 'outp', 0).u.words[0] == 1 && get(OBJECT_SPEAKER, 'dflt', 'inpt', 0).u.words[0] == 0);
	CHECK(get(OBJECT_CODEC, 'dflt', 'glob', 0).u.words[0] == 1 && get(OBJECT_SPEAKER, 'dflt', 'glob', 0).u.words[0] == 1);
	CHECK(get(OBJECT_HAWKING, 'dflt', 'glob', 0).u.words[0] == 0);   /* the actuator keeps its old answer */
	{ struct value w = get(OBJECT_CODEC, 'volm', 'inpt', 0);
	  CHECK(w.u.decimal == 1.0f && has(OBJECT_CODEC, 'volm', 'inpt', 1) && !has(OBJECT_CODEC, 'volm', 'inpt', 2)); }
	{ AudioObjectPropertyAddress a = { 'vold', 'inpt', 0 }; Float32 db = -12.0f;
	  CHECK(set_property_data(NULL, OBJECT_CODEC, 0, &a, 0, NULL, 4, &db) == 0);
	  CHECK(get(OBJECT_CODEC, 'volm', 'inpt', 0).u.decimal > 0.7f && get(OBJECT_CODEC, 'volm', 'inpt', 0).u.decimal < 0.8f); }
	CHECK(get(OBJECT_RECEIVER_MUTE, 'clas', 'glob', 0).u.words[0] == 'mute' && get(OBJECT_RECEIVER_MUTE, 'bcls', 'glob', 0).u.words[0] == 'togl');
	CHECK(get(OBJECT_RECEIVER_MUTE, 'cscp', 'glob', 0).u.words[0] == 'outp' && get(OBJECT_RECEIVER_MUTE, 'celm', 'glob', 0).u.words[0] == 3);
	CHECK(get(OBJECT_MIC_MUTE, 'cscp', 'glob', 0).u.words[0] == 'inpt' && get(OBJECT_MIC_MUTE, 'stdv', 'glob', 0).u.words[0] == OBJECT_CODEC);
	CHECK(get(OBJECT_RECEIVER_VOLUME, 'celm', 'glob', 0).u.words[0] == 3 && get(OBJECT_RECEIVER_VOLUME, 'stdv', 'glob', 0).u.words[0] == OBJECT_CODEC);
	CHECK(!has(OBJECT_SPEAKER_OUTPUT, 'taps', 'glob', 0));
	/* routes: speaker active + receiver unmuted + codec output active = both: nothing; codec inactive -> speaker */
	CHECK(set32(OBJECT_CODEC_OUTPUT, 'sact', 'glob', 0, 0) == 0); CHECK(ioctl_calls[2] == 1 && ioctl_last[2][0] == 1);
	CHECK(set32(OBJECT_CODEC_OUTPUT, 'sact', 'glob', 0, 1) == 0 && set32(OBJECT_SPEAKER_OUTPUT, 'sact', 'glob', 0, 0) == 0);
	CHECK(ioctl_calls[2] == 2 && ioctl_last[2][0] == 0);
	CHECK(set32(OBJECT_SPEAKER_OUTPUT, 'sact', 'glob', 0, 1) == 0 && ioctl_calls[2] == 2);       /* both on: kept */
	CHECK(set32(OBJECT_RECEIVER_MUTE, 'bcvl', 'glob', 0, 1) == 0 && ioctl_calls[2] == 3 && ioctl_last[2][0] == 1);   /* receiver muted: speaker */
	CHECK(set32(OBJECT_CODEC, 'mute', 'outp', 3, 0) == 0 && ioctl_calls[2] == 3);                 /* device-level: unmuted, both on */
	CHECK(get(OBJECT_RECEIVER_MUTE, 'bcvl', 'glob', 0).u.words[0] == 0);
	/* mic mute */
	CHECK(set32(OBJECT_MIC_MUTE, 'bcvl', 'glob', 0, 1) == 0 && ioctl_calls[4] == 1 && ioctl_last[4][0] == 1);
	CHECK(set32(OBJECT_CODEC, 'mute', 'inpt', 0, 0) == 0 && ioctl_calls[4] == 2 && ioctl_last[4][0] == 0);
	/* the speaker input's mute (the aggregate's input in a call): ORed with the codec's */
	CHECK(has(OBJECT_SPEAKER, 'mute', 'inpt', 0) && !has(OBJECT_SPEAKER, 'mute', 'outp', 0));
	CHECK(set32(OBJECT_SPEAKER, 'mute', 'inpt', 0, 1) == 0 && ioctl_calls[4] == 3 && ioctl_last[4][0] == 1);
	CHECK(set32(OBJECT_MIC_MUTE, 'bcvl', 'glob', 0, 1) == 0 && set32(OBJECT_SPEAKER_IN_MUTE, 'bcvl', 'glob', 0, 0) == 0 && ioctl_last[4][0] == 1);
	CHECK(set32(OBJECT_MIC_MUTE, 'bcvl', 'glob', 0, 0) == 0 && ioctl_last[4][0] == 0);
	CHECK(listed(get(OBJECT_SPEAKER, 'ownd', 'glob', 0), OBJECT_SPEAKER_IN_MUTE) && !listed(get(OBJECT_SPEAKER, 'ownd', 'outp', 0), OBJECT_SPEAKER_IN_MUTE));
	/* v3: input volume controls on the codec (element 0 and 1), channel-1 mute, any data source accepted */
	CHECK(get(OBJECT_MIC_VOLUME, 'cscp', 'glob', 0).u.words[0] == 'inpt' && get(OBJECT_MIC_VOLUME1, 'celm', 'glob', 0).u.words[0] == 1);
	CHECK(get(OBJECT_MIC_VOLUME, 'clas', 'glob', 0).u.words[0] == 'vlme' && get(OBJECT_MIC_VOLUME, 'stdv', 'glob', 0).u.words[0] == OBJECT_CODEC);
	CHECK(listed(get(OBJECT_CODEC, 'ownd', 'inpt', 0), OBJECT_MIC_VOLUME) && !listed(get(OBJECT_CODEC, 'ownd', 'outp', 0), OBJECT_MIC_VOLUME));
	{ int r3 = ioctl_calls[3]; CHECK(setf(OBJECT_MIC_VOLUME, 'lcsv', 0.5f) == 0 && ioctl_calls[3] == r3); }
	CHECK(has(OBJECT_CODEC, 'mute', 'inpt', 1) && !has(OBJECT_CODEC, 'mute', 'inpt', 2));
	CHECK(set32(OBJECT_CODEC, 'mute', 'inpt', 1, 1) == 0 && ioctl_last[4][0] == 1 && get(OBJECT_MIC_MUTE1, 'bcvl', 'glob', 0).u.words[0] == 1);
	CHECK(set32(OBJECT_MIC_MUTE1, 'bcvl', 'glob', 0, 0) == 0 && ioctl_last[4][0] == 0);
	{ AudioObjectPropertyAddress a = { 'scci', 'glob', 0 }; UInt32 two[2] = { 'imic', 'fmic' };
	  CHECK(set_property_data(NULL, OBJECT_CODEC_SOURCE, 0, &a, 0, NULL, 8, two) == 0); }
	/* receiver volume -> 'r' 3 port 0 */
	CHECK(setf(OBJECT_RECEIVER_VOLUME, 'lcsv', 0.75f) == 0 && ioctl_calls[3] == 1 && ioctl_last[3][0] == 0 && ioctl_last[3][1] == 96);
	CHECK(setf(OBJECT_SPEAKER_VOLUME, 'lcsv', 1.0f) == 0 && ioctl_calls[3] == 2 && ioctl_last[3][0] == 1 && ioctl_calls[1] == 1);
	/* v7: "Baseband Voice", the call's third device (RoutingHandler_Call_GenericConfig2) */
	{ struct value dl = get(OBJECT_PLUGIN, 'dev#', 'glob', 0); CHECK(listed(dl, OBJECT_BASEBAND) && dl.size == 5 * 4); }
	{ CFStringRef bb = CFSTR("Baseband Voice"); AudioObjectPropertyAddress a = { 'uidd', 'glob', 0 }; struct value u = property(OBJECT_PLUGIN, &a, sizeof bb, &bb);
	  CHECK(u.u.words[0] == OBJECT_BASEBAND); }
	CHECK(get(OBJECT_BASEBAND, 'dflt', 'glob', 0).u.words[0] == 0 && get(OBJECT_BASEBAND, 'dflt', 'outp', 0).u.words[0] == 0 &&
	      get(OBJECT_BASEBAND, 'dflt', 'inpt', 0).u.words[0] == 0 && get(OBJECT_BASEBAND, 'sflt', 'outp', 0).u.words[0] == 0);
	CHECK(get(OBJECT_BASEBAND, 'stm#', 'inpt', 0).u.words[0] == OBJECT_BASEBAND_INPUT && get(OBJECT_BASEBAND, 'stm#', 'outp', 0).u.words[0] == OBJECT_BASEBAND_OUTPUT);
	CHECK(get(OBJECT_BASEBAND_INPUT, 'sdir', 'glob', 0).u.words[0] == 1 && get(OBJECT_BASEBAND_OUTPUT, 'sdir', 'glob', 0).u.words[0] == 0);
	CHECK(get(OBJECT_BASEBAND, 'nsr#', 'glob', 0).size == 5 * sizeof(AudioValueRange) && get(OBJECT_BASEBAND, 'nsrt', 'glob', 0).u.number == 48000);
	CHECK(get(OBJECT_BASEBAND_INPUT, 'sfma', 'glob', 0).size == 5 * sizeof(AudioStreamRangedDescription));
	{ AudioObjectPropertyAddress a = { 'nsrt', 'glob', 0 }; Float64 r = 16000, bad = 44100;
	  CHECK(set_property_data(NULL, OBJECT_BASEBAND, 0, &a, 0, NULL, 8, &r) == 0 && get(OBJECT_BASEBAND, 'nsrt', 'glob', 0).u.number == 16000);
	  CHECK(get(OBJECT_BASEBAND_OUTPUT, 'sfmt', 'glob', 0).u.format.mSampleRate == 16000);
	  CHECK(set_property_data(NULL, OBJECT_BASEBAND, 0, &a, 0, NULL, 8, &bad) != 0);
	  CHECK(set_property_data(NULL, OBJECT_CODEC, 0, &a, 0, NULL, 8, &r) != 0 && get(OBJECT_CODEC, 'nsrt', 'glob', 0).u.number == 48000); }
	{ AudioObjectPropertyAddress a = { 'pft ', 'glob', 0 }; AudioStreamBasicDescription f = get(OBJECT_BASEBAND_INPUT, 'pft ', 'glob', 0).u.format;
	  f.mSampleRate = 8000; CHECK(set_property_data(NULL, OBJECT_BASEBAND_INPUT, 0, &a, 0, NULL, sizeof f, &f) == 0 && get(OBJECT_BASEBAND, 'nsrt', 'glob', 0).u.number == 8000); }
	{ int16_t buf[64]; memset(buf, 0x55, sizeof buf); int w = ioctl_calls[1];
	  CHECK(do_io_operation(NULL, OBJECT_BASEBAND, OBJECT_BASEBAND_INPUT, 0, 'read', 32, NULL, buf, NULL) == 0 && buf[0] == 0 && buf[31] == 0 && buf[32] == 0x5555);
	  CHECK(do_io_operation(NULL, OBJECT_BASEBAND, OBJECT_BASEBAND_OUTPUT, 0, 'rite', 32, NULL, buf, NULL) == 0 && baseband_frames == 32 && ioctl_calls[1] == w); }
	{ AudioObjectPropertyAddress a = { 'scci', 'glob', 0 }; UInt32 two[2] = { 'smic', 0 };
	  CHECK(set_property_data(NULL, OBJECT_CODEC_SOURCE, 0, &a, 0, NULL, 8, two) == 0);
	  struct value c = get(OBJECT_CODEC_SOURCE, 'scci', 'glob', 0); CHECK(c.size == 8 && c.u.source[0] == 'smic');
	  CHECK(listed(get(OBJECT_CODEC_SOURCE, 'scai', 'glob', 0), 'smic')); }
	{ Float64 st; UInt64 ht, seed; ticks_per_frame = 20.8; CHECK(get_zero_time_stamp(NULL, OBJECT_BASEBAND, 0, &st, &ht, &seed) == 0); }
	printf("plug-in object model: all checks passed\n");
	return 0;
}
CFUUIDRef CFUUIDGetConstantUUIDWithBytes(CFAllocatorRef a, UInt8 b0, UInt8 b1, UInt8 b2, UInt8 b3, UInt8 b4, UInt8 b5, UInt8 b6, UInt8 b7,
    UInt8 b8, UInt8 b9, UInt8 b10, UInt8 b11, UInt8 b12, UInt8 b13, UInt8 b14, UInt8 b15)
{ (void)a; (void)b0; (void)b1; (void)b2; (void)b3; (void)b4; (void)b5; (void)b6; (void)b7; (void)b8; (void)b9; (void)b10; (void)b11;
  (void)b12; (void)b13; (void)b14; (void)b15; return 0; }
int mach_timebase_info(mach_timebase_info_data_t *t) { t->numer = t->denom = 1; return 0; }
uint64_t mach_absolute_time(void) { return 0; }
static int err_; int *__error(void) { return &err_; }
