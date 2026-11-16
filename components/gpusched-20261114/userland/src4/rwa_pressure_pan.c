/*
 * y2s95 (pressure): the one piece of rwa_memory_pressure (air_metal.c) that needs Panfrost's own headers, compiled with
 * pan_screen.c's command (build.sh) and linked into libredwoodair.dylib. Called under rwa_lock.
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include "pan_screen.h"
#include "pan_bo.h"

void rwa_pan_evict_bo_cache(struct pipe_screen *screen);

/* Panfrost's BO cache of this screen emptied: the bo_map lock first, as panfrost_bo_unreference takes it before the cache lock */
void
rwa_pan_evict_bo_cache(struct pipe_screen *screen)
{
   struct panfrost_device *dev = pan_device(screen);

   pthread_mutex_lock(&dev->bo_map_lock);
   panfrost_bo_cache_evict_all(dev);
   pthread_mutex_unlock(&dev->bo_map_lock);
}
