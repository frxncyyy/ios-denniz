/* SPDX-License-Identifier: MIT */
/* TEST STAND-IN, NOT A DELIVERABLE: K01's kern/bsd/dev/nord2_jpegdev.inc as far as 0299 is concerned (K01's real file
 * creates /dev/redwood-jpeg; 0299 only calls its start function once, from nord2_input_init). */
static void
nord2_jpegdev_start(void)
{
	/* the real file creates /dev/redwood-jpeg here */
}
