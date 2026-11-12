/* SPDX-License-Identifier: MIT */
/* probe_arm.c -- compiled for armv7a with -ffreestanding: mtk_venc_ipi.h's _Static_asserts are the
 * check that the 32-bit AAPCS layout equals the 64-bit one.  Never linked or run. */
#include "mtk_venc_ipi.h"

int v41_arm_layout_probe;
