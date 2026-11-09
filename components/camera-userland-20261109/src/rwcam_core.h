/*
 * The reader of /dev/redwood-camera: one thread, one open descriptor, ONE sensor streaming at a time. Which camera streams is
 * whichever stream of the capture device (rwcam_device.c) the capture graph started last; when another is started the thread
 * sends REDWOOD_CAMERA_SENSOR and goes on with the other one's frames. No Apple types in here: the file is built unchanged into the
 * bundle and into the host test (test/t_core.c), which brings a fake kernel through struct rwcam_ops.
 */
#ifndef RWCAM_CORE_H
#define RWCAM_CORE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include "redwood_camera.h"
#include "camera_isp.h"

#define RWCAM_CAMERAS 3
enum { RWCAM_BACK_WIDE = 0, RWCAM_BACK_ULTRAWIDE = 1, RWCAM_FRONT = 2 };

/* the kernel's `sensor` number of each camera (redwood_camera.h: 1 back wide IMX766, 2 front IMX615, 3 back ultra-wide IMX355) */
extern const uint32_t rwcam_sensor_of[RWCAM_CAMERAS];

struct rwcam_ops {
	int (*open_dev)(void);                                  /* the descriptor, or -1 */
	ssize_t (*read_dev)(int fd, void *buf, size_t n);
	ssize_t (*write_dev)(int fd, const void *buf, size_t n);
	void (*close_dev)(int fd);
	int (*open_node)(int node);                             /* node 0: as open_dev; node 1: the second instance (NULL: there is none) */
};

/* called on the reader's thread for each good frame of the camera `cam` that is running; isp is that camera's own ISP state */
struct rwcam_sink {
	void (*frame)(void *context, int cam, const struct redwood_camera_frame *f, const uint8_t *raw, const struct isp *isp);
	void (*stopped)(void *context, int cam, const char *why);       /* the thread gave up on this camera (a fault) */
	void *context;
};

void rwcam_core_init(const struct rwcam_ops *ops, const struct rwcam_sink *sink);   /* ops NULL: the real device */
/* camera `cam` is wanted (it becomes the one that streams; the previous one is dropped) / not wanted any more */
bool rwcam_core_start(int cam);
void rwcam_core_stop(int cam);
int rwcam_core_owner(void);                                     /* -1: none */
/* test and diagnostics */
uint64_t rwcam_core_switches(void);
uint64_t rwcam_core_frames(int cam);
uint64_t rwcam_now_ms(void);                                     /* milliseconds that only go forward (mach_absolute_time on the phone) */
uint64_t rwcam_ns_units(uint64_t ns);                           /* nanoseconds as mach_absolute_time()'s units */
uint64_t rwcam_core_kicks(void);
/*
 * Autofocus (a camera whose frames say focus_max: the kernel can move its lens): the window the sharpness is taken of, as its middle and size
 * in parts of the frame (0 to 1), and whether to look for the sharpest position again now (a tap; otherwise only when the picture has changed).
 */
void rwcam_core_focus_window(int cam, float cx, float cy, float w, float h, bool again);
void rwcam_core_focus_hold(int cam, bool hold);                 /* the lens stays where the scan leaves it / follows the scene again */
/* the exposure goes by this window of the frame (w or h 0: by all of it); locked: it stays as it is */
void rwcam_core_meter_window(int cam, float cx, float cy, float w, float h);
void rwcam_core_exposure_lock(int cam, bool locked);                                /* kernel sessions started anew because the frames had stopped */
void rwcam_core_shutdown(void);                                 /* stop the threads and wait for them */

/*
 * The PAIR: the two back cameras running at once on the kernel's two nodes (rwcam_core.c), for CMCapture's dual-wide device. `shown' is the one
 * whose frames are read whole and given to the sink; of the other only the headers are read. rwcam_core_pair_show() changes which, at once (the
 * next frame of the camera now shown). pair_start is false when there is no second node: the caller then uses rwcam_core_start() as ever, and
 * so does the core by itself (pair_show() included) when a node of a running pair gives up.
 */
bool rwcam_core_pair_possible(void);
bool rwcam_core_pair_start(int shown);
bool rwcam_core_pair_running(void);
void rwcam_core_pair_show(int cam);
void rwcam_core_pair_slave_every(unsigned n);                   /* the hidden camera's every n-th frame goes to the sink as well (0: none) */
void rwcam_core_pair_stop(void);
uint64_t rwcam_core_pair_shows(void);
uint64_t rwcam_core_headers(int cam);                           /* frames of which only the header was read */

/* what a camera's last frame was (a hidden camera's too), for the metadata CMCapture's stream selector goes by */
struct rwcam_status {
	uint64_t frames;                /* 0: nothing yet since the camera was taken up */
	uint64_t sequence, time;
	uint64_t exposure_ns;
	float gain, digital;            /* the sensor's analog gain, and what the picture gets on top */
	float level, target;            /* green above black over white, with the digital gain; and what AE steers it to */
	float fps;
	float wb[3];
	uint32_t focus, focus_max;
	uint32_t quiet;                 /* frames since AE last asked for another exposure */
	bool ae_stable;                 /* ... three or more */
	bool focus_idle;                /* no scan for the sharpest position under way */
	bool whole;                     /* the frame was read whole (not its header only) */
};
void rwcam_core_status(int cam, struct rwcam_status *out);

/*
 * The ultra-wide camera's zoom. Camera.app shows the ultra-wide camera as "0.5x" and the wide one as "1x" whatever the lenses are: its zoom control
 * goes by AVGestalt's SuperWideCameraToWideCameraRelativeZoomFactor of the model it is told it runs on (2.0 for every iPhone with an ultra-wide
 * camera), so at zoom 2.0 of the ultra-wide AVCaptureDevice it takes the picture to be the wide camera's at 1.0 and changes to that device. The Nord 2's
 * two lenses are not 2.0 apart (wide 24 mm, ultra-wide ~15 mm equivalent: RWCAM_UW_RATIO, nominal -- not measured on this phone), so the ultra-wide
 * stream does not crop by the graph's zoom itself: rwcam_view_zoom() is the crop that shows all of the sensor at zoom 1, the wide camera's field of
 * view at RWCAM_UW_SWITCH_ZOOM, and goes evenly (the same ratio for the same ratio of zoom) between them; past the switch it follows the graph's
 * zoom again. Every other camera: the graph's zoom as it is.
 */
#define RWCAM_UW_SWITCH_ZOOM    2.0f
#define RWCAM_UW_RATIO          1.6f            /* wide focal length / ultra-wide focal length, both as 35 mm equivalents */
float rwcam_view_zoom(int cam, float graph_zoom, float uw_ratio);

/*
 * The two back cameras' pictures on one another (run q10ht: "the zoom from one lens to the other must be smooth"). While the pair runs the
 * reader of the hidden camera now and then reads a whole frame, makes a small picture of it and of the shown camera's frame of that moment,
 * and looks for the place of the wide picture in the ultra-wide one (rwcam_align.h): the lenses' ratio and how far their axes are apart.
 * rwcam_core_alignment(): what is known (found: how many findings it is the middle of; 0: the nominal RWCAM_UW_RATIO and no offset, or what
 * rwcam_core_align_set() gave). rwcam_uw_view(): the ultra-wide camera's window for the device's zoom -- all of its sensor at 1, exactly the
 * wide camera's field at RWCAM_UW_SWITCH_ZOOM (so that the change of camera there is not seen), evenly between; its middle moves from the
 * sensor's middle to the wide camera's axis on the way.
 */
void rwcam_core_alignment(float *ratio, float *dx, float *dy, unsigned *found);
/*
 * The hidden camera's frames read whole as well (not given to the sink), for as long as this is on: the device's zoom is near the change of
 * camera (run q10hy: "a hesitation, a small freeze" there). A camera read by its headers needs a frame's time to be read whole, and its
 * exposure went by the header's levels, which have no highlights; read whole it is as ready as the shown one, and the change is the next frame.
 */
/* (run user1: at rest at "1x", which is where the sensors change, the hidden one was read whole for ever -- a copy of its frame and its
 * measures 17 to 30 times a second. Warm lasts RWCAM_WARM_MS from the last call: the zoom has to be moving) */
#define RWCAM_WARM_MS 2500
void rwcam_core_pair_warm(bool warm);
/* by hand (the markers): ratio > 1 fixes all three and stops the looking; ratio 0: looking again */
void rwcam_core_align_set(float ratio, float dx, float dy);
void rwcam_uw_view(float device_zoom, float ratio, float dx, float dy, float *zoom, float *shift_x, float *shift_y);
/* the mains' light: exposures are whole multiples of this many ns when they can be (10000000: 50 Hz, 8333333: 60 Hz, 0: any) */
void rwcam_core_flicker(uint32_t ns);
/* the graph wants every frame as long as the mode's own (a film at 30 a second): no longer frames in the dark, gain only */
void rwcam_core_even_rate(bool even);

#endif
