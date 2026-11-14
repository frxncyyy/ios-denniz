/*
 * What the phone's ISP would do with the cameras' RAW10 frames, done by the processor instead
 * (src/camera_isp.c): the levels of a frame, the exposure for the next ones (AE), the white balance
 * (AWB), and a picture: each 2x2 Bayer cell one pixel of colour, scaled to the size asked for, or
 * every pixel (bilinear demosaic) at the sensor's own size; lens shading (a grid of gains per
 * colour), the white balance's and AE's gains, a colour matrix, a gamma; as 8-bit YCbCr 4:2:0 (NV12,
 * full range: CoreVideo's 420f, or video range: 420v) or BGRA.
 */
#ifndef CAMERA_ISP_H
#define CAMERA_ISP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "redwood_camera.h"

struct isp_stats {
	double          mean[3];        /* R, G, B above black, in 10-bit steps */
	double          bright;         /* the level of green that 1 % of the cells are above */
	uint32_t        hist[64];       /* green above black, 16 steps a bin */
	uint32_t        cells;
	double          fair[3];        /* R, G, B means of the cells that are neither clipped nor near black: what the white balance goes by */
	uint32_t        fair_cells;
	double          grey[3];        /* and of those of them that could be a grey thing in some real light (green 1 to 2.8 times red, 1.1 to 3.6
	                                 * times blue): a yellow wall, a blue sky, a lawn are not among them */
	uint32_t        grey_cells;
};

/*
 * Autofocus by contrast, for a lens the kernel can move (the frames' focus_max not 0), as a phone's continuous autofocus goes about it
 * (run q10hz: "it works badly and rarely, and the breathing is too marked, long and jerky" -- every search began with the lens at 0 and
 * went over the whole range in tenths of it, three frames each, and began only when the sharpness had been 40 % off for 20 frames):
 *   - the sharpness of a window of the frame (isp_sharpness: the squared differences of neighbouring cells' brightness over the squared
 *     brightness, the same at any exposure);
 *   - watching: the sharpness the lens was left with is remembered; off it by more than a quarter (or up by 45 %) for ISP_AF_OFF frames,
 *     and steady again from one frame to the next, a search begins (isp_af_again: at once -- a tap);
 *   - the search, from where the lens IS: a small step one way (a fortieth of the range), the other way if that was not better; then on in
 *     the better direction for as long as it gets sharper, the step doubling after three such steps (a lens far off gets there in a few);
 *     once past the peak, to the top of the curve through the last three places. Neither way better: the lens stays (a picture too dark
 *     or too plain to tell never moves it). Neither way better NOR worse, and the picture far less sharp than it was left (or never in
 *     focus, or a tap): the lens is too far off for a small step to tell, and goes over the range without stopping -- up from where it
 *     is, then down --, the sharpness of every frame taken with the place its lens was at, until a clear peak is behind it; then to the
 *     best place and the small steps from there;
 *   - the lens goes at most ISP_AF_SLEW of its range a frame, wherever it goes, and ISP_AF_SETTLE frames pass after it has arrived before
 *     a sharpness is believed.
 */
#define ISP_AF_SETTLE   0               /* (the frame that says the lens is there is looked at: isp_af) */
#define ISP_AF_OFF      10
#define ISP_AF_SLEW     32              /* (the range's share a frame: a 32nd) */
struct isp_af {
	uint32_t        state;          /* 0 watching, 1 the step one way, 2 the step the other way, 3 on in the better direction, 4 to the top;
	                                 * 5 over the range (the lens too far off for a small step to tell), 6 to the best place of that */
	uint32_t        wait;           /* frames still to let pass */
	uint32_t        pos, target;    /* where the lens was last told to be, and where it is on its way to */
	uint32_t        seen;           /* where the lens was for THIS frame (the frame's `focus'; the caller's, before every call) */
	/*
	 * Run q10ia ("it practically never acts by itself; tapping works"): one frame's sharpness is off the next one's by a third and more
	 * (noise, the hand), so "a quarter off for 8 frames and steady" hardly ever was, and "neither way better NOR worse" never. Now: the
	 * watching goes by a running mean of it (sm); every place of a search is looked at for two frames; and the scene's own brightness
	 * (light: the level over exposure x gain, the caller's before every call; 0: not known) having changed by a third is a reason too.
	 */
	double          light, light_ref;
	double          sm, acc;
	uint32_t        acc_n, fresh;
	double          held_before;    /* what the lens had been left with when this search began */
	bool            swept;          /* this search has been over the range */
	bool            turned;         /* ... and has turned at one end of it */
	uint32_t        late;           /* frames since the lens arrived that do not say its position yet */
	bool            swept_again;    /* (the small steps that follow a look over the range are the same search) */
	bool            pos_known;      /* (pos is the lens's: the caller gives the frame's `focus' until the first move) */
	uint32_t        origin, step;
	int             dir;
	uint32_t        run, falls;     /* steps in a row that were sharper / less sharp */
	uint32_t        p[3];           /* the last three places of the search, and their sharpness */
	double          s[3];
	uint32_t        n;
	double          s0;             /* the sharpness at the origin */
	uint32_t        best_at;
	double          best, held;     /* the best sharpness of the search; the sharpness the lens was left with */
	double          before;         /* the frame before's (watching: is the picture steady?) */
	uint32_t        off, calm;      /* frames in a row whose sharpness is far from `held'; ... and near the frame before's */
	uint32_t        scans;
	bool            again, tapped;  /* a search asked for (isp_af_again); ... by the user, this one */
	bool            hold;           /* the lens stays where a search left it (focus locked, or a single autofocus): no watching */
};

/*
 * Lens shading: a gain for each colour at each point of a grid laid over the sensor (corners on its
 * corners), between the points bilinear. Off until it is given one (isp_lsc_from_flat()).
 */
#define LSC_W           17
#define LSC_H           13
struct isp_lsc {
	bool            on;
	float           gain[3][LSC_H][LSC_W];
};

struct isp {
	float           wb[3];          /* the white balance's gains, green 1 */
	float           digital;        /* more gain, for what AE cannot reach with the sensor's */
	float           ccm[3][3];      /* the colour matrix, camera RGB (white balanced) to linear sRGB; rows sum to 1 */
	struct isp_lsc  lsc;
	uint8_t         gamma[4096];    /* linear (white above black is 3836) to 8 bits, sRGB's curve */
	uint32_t        asked_lines, asked_gain, asked_frame_lines;     /* what AE asked for last */
	bool            asked;
	uint32_t        asked_waits;    /* frames in a row that did not have what was asked for */
	float           wb_day[2], wb_warm[2];  /* red's and blue's gains in daylight and in a warm lamp's light: the line the white balance keeps to */
	float           wb_adapt;       /* how much of a warm light's warmth is taken out (1: all) */
	bool            wb_free;        /* grey world as it comes, no line (as until run q10hg) */
	float           falloff;        /* the lens's falloff undone: the gain at the frame's corners less 1 (0: none), by the square of the distance */
	float           zoom;           /* the pictures show the middle 1/zoom of what they would (1 and under: all of it), taken from the sensor's
	                                 * own pixels: no interpolation up to twice the cells' scale */
	uint32_t        flip;           /* ISP_FLIP_H: left and right exchanged, ISP_FLIP_V: top and bottom (a sensor mounted the other way round) */
	uint32_t        max_frame_lines;        /* the longest frame AE may ask for (0: twice the shortest) */
	/*
	 * The window's place: the middle of what is shown is moved by this share of the unzoomed picture's width / height (0, 0: the middle of
	 * the sensor, as ever; kept inside the sensor). For the ultra-wide camera's picture to lie on the wide camera's (rwcam_core.h).
	 */
	float           shift_x, shift_y;
	/* AE (isp_ae): frames seen with what was asked for since it was; the exposure is where it should be; the mains' light's period */
	uint32_t        settled;
	bool            steady;
	uint32_t        flicker_ns;     /* exposures are whole multiples of this when they can be (10 ms: 50 Hz mains; 0: any) */
	/*
	 * The highlights (run q10hw: "the 0.5x is overexposed, the main one is fine"). aim: the share of its target AE is going for now, 1 or
	 * down to a half when the picture's brightest 1 % would be white otherwise. hidden_aim: the same, told from outside, for levels that
	 * come without their brightest 1 % (a hidden camera's, from the kernel's header): the other back camera's, which sees the same lamps.
	 */
	float           aim, hidden_aim;
	/*
	 * (ae2, run lm6: "the lights flicker badly in strong light" -- IMG_0065.MOV, luma 48..133 in steps every 3-4 frames, period 20-25 frames)
	 * ae_seq: the frame whose levels AE had last (the GPU's statistics of a frame seen already are not news); ae_m: the levels of the last three
	 * frames since the exposure settled (averaged: the mains' beat repeats every 3 frames at 30 and at 60 a second); flicker_hold: frames
	 * left in which the exposure keeps to one period of the mains (their beat was seen under it); digital_next: the digital gain of the last
	 * command, put into the pictures when the sensor has the command, not before.
	 */
	uint64_t        ae_seq;
	float           ae_m[3];
	uint32_t        ae_n;
	uint32_t        flicker_hold;
	float           digital_next;
};
#define ISP_AE_GAIN_FIRST       4.0     /* the gain at which frames get longer, when they may */
#define ISP_AE_GAIN_MOST        16.0    /* the most analog gain asked of a sensor, whatever it would take */
#define ISP_AE_DIGITAL_MOST     2.0
#define ISP_AE_SETTLE   2               /* frames with the new exposure in their header before AE believes their levels */

#define ISP_FLIP_H      1u
#define ISP_FLIP_V      2u

void isp_init(struct isp *isp);
/* the levels, from every 4th cell of every 4th pair of lines */
/* ... and of a window of the frame only (pixels; clipped to the frame): what the exposure goes by after a tap */
void isp_measure_window(const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t x, uint32_t y, uint32_t w, uint32_t h, struct isp_stats *s);
void isp_measure(const struct redwood_camera_frame *f, const uint8_t *raw, struct isp_stats *s);
/* the exposure for the frames to come: true, and the command filled in, when it should change */
bool isp_ae(struct isp *isp, const struct redwood_camera_frame *f, const struct isp_stats *s, struct redwood_camera_command *c);
/* the command AE gave last, again (the kernel's session was started anew: its sensor is back at the exposure it starts with); false if none */
bool isp_ae_again(const struct isp *isp, struct redwood_camera_command *c);
/* the sharpness of the window x, y, w, h (pixels of the frame; clipped to it) */
double isp_sharpness(const struct redwood_camera_frame *f, const uint8_t *raw, uint32_t x, uint32_t y, uint32_t w, uint32_t h);
/* one frame's sharpness: true, and *move the position the lens is to go to, when it should move */
bool isp_af(struct isp_af *af, double sharpness, uint32_t focus_max, uint32_t *move);
void isp_af_again(struct isp_af *af);
/* the white balance: grey world, a little of the way at a time */
void isp_awb(struct isp *isp, const struct isp_stats *s);
/*
 * A picture of the frame, width x height, of the sensor's middle part with the picture's proportions
 * (rows from first_row to end_row only, for doing it in pieces). NV12: y and uv planes, full range (420f) or video
 * range (420v); BGRA: y only.
 */
void isp_render_nv12(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row, bool video_range);
void isp_render_bgra(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *out, size_t stride,
    uint32_t width, uint32_t height, uint32_t first_row, uint32_t end_row);
/*
 * Every pixel at the sensor's own size (f->width x f->height): the two colours a site does not have
 * from the same colour's sites around it (bilinear); rows from first_row to end_row (even), NV12.
 */
void isp_render_full_nv12(const struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, uint8_t *y, size_t y_stride,
    uint8_t *uv, size_t uv_stride, uint32_t first_row, uint32_t end_row, bool video_range);
/*
 * Lens shading measured from a frame of a flat field (a white sheet held over the lens, evenly lit):
 * each colour's level at each grid point (a block's mean) over the brightest of its own; false if
 * the frame is too dark or clipped for it. min_ratio: the darkest point's level over the brightest's.
 */
bool isp_lsc_from_flat(struct isp *isp, const struct redwood_camera_frame *f, const uint8_t *raw, float *min_ratio);
void isp_set_ccm(struct isp *isp, const float m[9]);
#ifdef ISP_LINES
extern int isp_reference;       /* not 0: isp_render_nv12 pixel by pixel, as it was (the tests compare the two) */
#endif
/*
 * A two-plane 4:2:0 picture made smaller, from one already made (a film's viewfinder from the film's picture of the same frame: no second
 * pass over the sensor's frame). Rows row0 to row1 (even) of the smaller one's luma and the chroma rows that go with them. Each point is
 * the mean of its four neighbours by distance; of sixteen when the picture gets more than twice smaller. range: 0 the same, 1 video (16 to
 * 235) to full, 2 full to video.
 */
void isp_shrink_420(const uint8_t *const from[2], const size_t from_stride[2], uint32_t from_width, uint32_t from_height, uint8_t *const to[2],
    const size_t to_stride[2], uint32_t width, uint32_t height, uint32_t row0, uint32_t row1, int range);

#endif
