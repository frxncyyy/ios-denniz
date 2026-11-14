/*
 * What CMCapture's synchronized streams group of the two back cameras (the dual-wide device) asks of the reader, worked out from what the group
 * and its two streams are told (rwcam_device.c's group object hands it on; no Apple types here: built unchanged into the host test, test/t_pair.c):
 *   - ActiveStreams with both streams, and one of them started: the PAIR runs (rwcam_core_pair_start), the group's master shown;
 *   - MasterConfiguration's Master: the other camera is shown, at once (rwcam_core_pair_show);
 *   - a slave whose configuration says StreamingOutputsEnabled: its pictures too, every MasterToSlaveFrameRateRatio-th frame; otherwise the slave
 *     gives no picture at all (its sensor runs, its headers are read: rwcam_core.c);
 *   - no second node, or it gave up: the master alone on the one node, a change of master a change of sensor, as before there was a pair;
 *   - fewer than both streams active, or none started: nothing of the group's runs.
 */
#ifndef RWCAM_PAIR_H
#define RWCAM_PAIR_H

#include <stdbool.h>
#include <pthread.h>
#include "rwcam_core.h"

#define RWCAM_PAIR_WARM_FROM    1.55f
#define RWCAM_PAIR_WARM_TO      2.6f
enum { RWCAM_PAIR_IDLE = 0, RWCAM_PAIR_BOTH = 1, RWCAM_PAIR_ALONE = 2 };

struct rwcam_pair {
	pthread_mutex_t lock;
	bool            active[2];      /* [0] the wide stream, [1] the ultra-wide one: in the group's ActiveStreams */
	bool            started[2];
	int             master;         /* RWCAM_BACK_WIDE or RWCAM_BACK_ULTRAWIDE */
	bool            slave_outputs;
	unsigned        slave_ratio;
	int             mode;           /* RWCAM_PAIR_*: what was asked of the reader last */
	/*
	 * Which SENSOR's frames are the pictures, by the device's zoom and not by the master (run q10ht: the zoom from one lens to the other
	 * has to be smooth). CMCapture changes its master early and with a margin of picture this capture device does not have (to the wide
	 * stream at about 1.83, to the ultra-wide one as soon as the zoom is under 2): with the master's own sensor shown, the picture jumped
	 * there. Under RWCAM_UW_SWITCH_ZOOM the ultra-wide sensor is shown, from it on the wide one -- where the two pictures are the same
	 * (rwcam_uw_view) -- and the pictures go to whichever stream is the master. No zoom told yet: the master's sensor.
	 */
	float           zoom;
	bool            zoom_known;
	int             shown;
	/*
	 * Near the change (the zoom between RWCAM_PAIR_WARM_FROM and RWCAM_PAIR_WARM_TO) the sensor that is not shown is read whole too
	 * (rwcam_core_pair_warm): ready, exposed by its own frames, and shown from the very next one. Away from it: its headers only, as ever.
	 */
	bool            warm;
	bool            no_pair;        /* the pair could not be had (or ended by itself): not asked for again until the group is idle */
	unsigned long   changes, masters;
};

void rwcam_pair_init(struct rwcam_pair *p);
void rwcam_pair_set_active(struct rwcam_pair *p, bool wide, bool ultra);
void rwcam_pair_set_master(struct rwcam_pair *p, int cam);
void rwcam_pair_set_slave(struct rwcam_pair *p, bool outputs, unsigned ratio);
/* a stream of the group was started / stopped; false: the reader could not be started */
bool rwcam_pair_set_started(struct rwcam_pair *p, int cam, bool started);
bool rwcam_pair_wanted(struct rwcam_pair *p);           /* both streams are active: the group's, not each stream's own, business */
void rwcam_pair_set_zoom(struct rwcam_pair *p, float device_zoom);
bool rwcam_pair_delivers(struct rwcam_pair *p, int cam);        /* this camera's (sensor's) frames are somebody's pictures now */
/* the camera whose STREAM gets the pictures of this sensor's frames (the master for the shown sensor; -1: nobody's) */
int rwcam_pair_route(struct rwcam_pair *p, int cam);
int rwcam_pair_shown(struct rwcam_pair *p);
int rwcam_pair_mode(struct rwcam_pair *p);
int rwcam_pair_master(struct rwcam_pair *p);

#endif
