/*
 * Metal's own shader cache survives a reboot.
 *
 * Metal keeps the results of its compiler service (the specialised CoreAnimation functions: 19 ms each on the display thread, 440 ms for the
 * first) in a file-system cache, libCoreFSCache's <name>.data / <name>.list pair (backboardd: .../Containers/Data/System/<uuid>/Library/Caches/
 * libraries.data and libraries.list).  libCoreFSCache throws that cache away at every boot (it compares kern.bootsessionuuid with the one it was
 * written under) unless the environment variable FS_CACHE_REUSE_ON_REBOOT is set; found in the dyld cache's libCoreFSCache.dylib and measured on the
 * phone: with the variable the display thread's specialisation stalls went from 27 calls / 1.7 s at boot and 44 calls / 0.9 s in the first
 * interactions to 2 calls / 28 ms and none.  This constructor runs when Metal loads the device plug-in, before the first compile, in every process
 * that uses Metal, and sets the variable.  Nothing else.
 *
 * Copyright (c) 2026 ios-sweet contributors.  SPDX-License-Identifier: MIT.
 */
#include <stdlib.h>

__attribute__((constructor)) static void
nord2_fscache_install(void)
{
	setenv("FS_CACHE_REUSE_ON_REBOOT", "1", 0);
}
