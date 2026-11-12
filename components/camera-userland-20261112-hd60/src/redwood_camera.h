/*
 * /dev/redwood-camera: the phone's wide camera for iOS (boot-arg cam=5; bsd/dev/redwood_camera.c).
 *
 *   open()   the first one powers the sensor up and starts its frames (the sensor's mode: boot-arg
 *            cam_mode, else the module file's 4000x3000 one); the last close puts everything down
 *            again, and so does a second without a read.
 *   read()   waits (at most a second) for the newest frame not read yet: a struct
 *            redwood_camera_frame, then as many of the frame's bytes as the buffer has room for.
 *            A buffer of the header's size learns the frame's size and nothing else is copied.
 *            EIO when the camera has had a fault: it stays off for the rest of the run.
 *   write()  commands: a struct redwood_camera_command, then what it says follows.
 */
#ifndef _SYS_REDWOOD_CAMERA_H_
#define _SYS_REDWOOD_CAMERA_H_

#include <stdint.h>

#define REDWOOD_CAMERA_DEVICE           "/dev/redwood-camera"
#define REDWOOD_CAMERA_DEVICE2          "/dev/redwood-camera2"  /* a second, independent instance with the same ABI (kernel q10hj): by itself
                                                                 * the ultra-wide sensor (3); a sensor the other node has gives EBUSY */
#define REDWOOD_CAMERA_MAGIC            "RWFRAME1"

struct redwood_camera_frame {
	char            magic[8];       /* REDWOOD_CAMERA_MAGIC */
	uint32_t        header_bytes;   /* this structure's size: the frame's bytes follow */
	uint32_t        width, height;  /* pixels */
	uint32_t        stride;         /* bytes from one line to the next */
	uint32_t        format;         /* 1: MIPI RAW10, four pixels in five bytes (four high bytes, then the four pairs of low bits);
	                                 * 4: RAW10 as the MT6893's CAMSV DMA packs it (PAK mode 2): four pixels as ONE little-endian 40-bit word,
	                                 * pixel k in bits 10k..10k+9 -- the DMA buffer copied out as it is, nothing repacked in the kernel */
	uint32_t        bayer;          /* the order of a 2x2 cell: 0 RGGB, 1 GRBG, 2 GBRG, 3 BGGR */
	uint32_t        black;          /* the black level, in 10-bit steps */
	uint64_t        sequence;       /* the frame's number since the sensor started, from 1 */
	uint64_t        time;           /* when the frame was done, in mach_absolute_time()'s units */
	uint32_t        exposure_lines; /* the exposure it was taken with, in lines ... */
	uint32_t        line_ns;        /* ... of this many nanoseconds */
	uint32_t        gain;           /* the sensor's analog gain register ... */
	uint32_t        gain_unity;     /* ... which is this for 1x */
	uint32_t        gain_max;       /* the most it may be given */
	uint32_t        frame_lines;    /* the frame's length in lines (frames a second: 1e9 / (frame_lines * line_ns)) */
	uint32_t        frame_lines_min; /* the mode's own length: the shortest frame */
	uint32_t        frame_lines_max; /* the longest frame it may be given */
	uint32_t        margin;         /* exposure_lines is at most frame_lines - margin */
	uint32_t        sensor;         /* which: 0 the macro, 1 the wide, 2 the front, 3 the ultra-wide camera */
	uint32_t        focus;          /* where the lens is (the position last given with REDWOOD_CAMERA_FOCUS, once the actuator has taken it) */
	uint32_t        focus_max;      /* the positions go from 0 to this; 0: this sensor's lens does not move (or the kernel cannot move it) */
	/*
	 * (kernel q10hj, KERNEL_ABI_DUAL.md; these were reserved[4], zero in older kernels) The frame's levels on an 8x8 grid of 2x2 Bayer cells
	 * spread over all of it, black taken off, the sensor's own (no digital gain): in every header, so that the exposure of a camera whose
	 * pictures are not shown can be kept right from reads of the header alone.
	 */
	uint32_t        stat_samples;   /* how many cells (64); 0: no levels here */
	uint32_t        stat_green;     /* the mean of the cells' greens above black, in 10-bit steps */
	uint32_t        stat_red;
	uint32_t        stat_blue;
};

#define REDWOOD_CAMERA_FORMAT_MIPI_RAW10        1
#define REDWOOD_CAMERA_FORMAT_LE40_RAW10        4

enum {
	REDWOOD_CAMERA_EXPOSURE = 1,    /* a: lines, b: gain, c: frame lines (0: the shortest that holds the exposure) */
	REDWOOD_CAMERA_KEEP = 2,        /* a picture for the host: a width, b height, c stride, d format (1 the frame's RAW10, 2 NV12,
	                                 * 3 BGRA); bytes follow */
	REDWOOD_CAMERA_FOCUS = 4,       /* a: the lens's position, 0 to the frames' focus_max (which end is far is not said: the bundle finds the
	                                 * sharpest); the frames' `focus` is this once the actuator has been given it */
	REDWOOD_CAMERA_SENSOR = 3,      /* another sensor: a which (as in the frames' header), b its mode (~0: the usual one);
	                                 * the frames stop, everything goes down, and the next read starts the other one */
	REDWOOD_CAMERA_MODE = 5,        /* (kernel 0340, q10jm) the main camera's sensor mode: a = 0 4096x3072 30/s, 1 2048x1152 60/s (hs_video),
	                                 * 2 4096x2304 60/s (custom2); like SENSOR: the frames stop and the next read starts it in that mode
	                                 * (EINVAL: a kernel without modes) */
};

struct redwood_camera_command {
	uint32_t        command;
	uint32_t        bytes;          /* of what follows this structure */
	uint32_t        a, b, c, d;
};

#endif /* _SYS_REDWOOD_CAMERA_H_ */
