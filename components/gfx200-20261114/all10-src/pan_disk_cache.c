/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
/*
 * gfxall3 (nord2, 14 Nov 2026): Panfrost's disk-cache hooks (panfrost_shader_get: retrieve before compiling a variant, store after)
 * served by a plain file cache in /private/var/db/nord2-metal/cache, because this build has no Mesa shader cache
 * (-Dshader-cache=disabled: disk_cache_create is a stub). Pipeline creation on the phone was ~5 ms each (perf1a: 227 pipelines, the
 * Valhall backend compile being most of it, host measure 0.3-6 ms per 0.4-5 KB shader), paid again by every process at every launch:
 * the first-use hitches. Key = SHA-1 over a tag (this file's format + the compiler build, N2PC_TAG: bump it with any compiler change),
 * the GPU id, Panfrost's own NIR SHA-1 of the uncompiled shader (pan_shader.c panfrost_alloc_shader) and the variant key -- the very
 * inputs Mesa's own disk cache keys on. File = header (magic, version, key, payload length, payload SHA-1) + the payload Mesa's cache
 * stores (binary length, binary, pan_shader_info, sysvals); every field checked on read, anything else is a miss. Written as a
 * private temporary file then renamed (atomic); a process that cannot write the directory (sandbox) still reads it.
 * Off: /private/var/db/nord2-metal/no-gfxall3-shadercache (read once per process), or the directory missing.
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "compiler/nir/nir.h"
#include "util/blob.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"

#include "pan_context.h"

#ifndef N2PC_DIR   /* (the host test points both elsewhere) */
#define N2PC_DIR     "/private/var/db/nord2-metal/cache"
#define N2PC_OFF     "/private/var/db/nord2-metal/no-gfxall3-shadercache"
#endif
#define N2PC_TAG     "nord2-pancache-2 mesa-25.0.7 gfxall4 valhall"
#define N2PC_MAGIC   0x4350324eu   /* 'N2PC' */
#define N2PC_VERSION 1u
#define N2PC_MAX_FILE (2u << 20)
#define N2PC_MAX_STORES 4096u
#ifndef N2PC_RO
#define N2PC_RO      "/usr/lib/nord2-metal-cache"   /* gfxall4: read-only seed shipped by the payload, readable by sandboxed apps */
#endif
extern int nord2_push_all_on(void);               /* pan_shader.c (gfxall4): pushed UBOs change the binary */

/* counters since load: 0 hits, 1 misses, 2 stores, 3 rejected (bad file), 4 store failures, 5 compile us (misses), 6 read us (hits),
 * 7 mode (0 off, 1 read only, 2 read+write) */
uint64_t nord2_pancache_count[8];
static int n2pc_mode = -1;
static uint64_t n2pc_t_miss;
/* gfxall4: where entries are written (the shared directory, else the process's own Library/Caches -- a sandboxed app's container --
 * so apps get hits from their second launch), and whether the read-only seed directory exists. Counter 7 = mode: 0 off, 1 read
 * only, 2 shared rw, 3 own rw; +4 when the read-only seed is present. */
static char n2pc_dir[512] = N2PC_DIR;
static int n2pc_ro;

static uint64_t
n2pc_now_us(void)
{
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static int
n2pc_init(void)
{
   if (n2pc_mode >= 0)
      return n2pc_mode;
   n2pc_mode = 0;
   if (access(N2PC_OFF, F_OK) == 0)
      return n2pc_mode;
   n2pc_ro = access(N2PC_RO, R_OK | X_OK) == 0;
   if (access(N2PC_DIR, R_OK | W_OK | X_OK) == 0) {
      n2pc_mode = 2;
   } else {
      const char *home = getenv("HOME");
      char p[512];
      if (home && home[0] == '/' && strlen(home) < 400) {
         snprintf(p, sizeof(p), "%s/Library/Caches", home);
         if (access(p, W_OK | X_OK) == 0) {
            snprintf(n2pc_dir, sizeof(n2pc_dir), "%s/Library/Caches/com.nord2.pancache", home);
            if ((mkdir(n2pc_dir, 0755) == 0 || errno == EEXIST) && access(n2pc_dir, R_OK | W_OK | X_OK) == 0)
               n2pc_mode = 3;
         }
      }
      if (!n2pc_mode) {
         snprintf(n2pc_dir, sizeof(n2pc_dir), "%s", N2PC_DIR);
         n2pc_mode = access(N2PC_DIR, R_OK | X_OK) == 0 ? 1 : (n2pc_ro ? 1 : 0);
      }
   }
   nord2_pancache_count[7] = (uint64_t)n2pc_mode + (n2pc_ro ? 4u : 0u);
   return n2pc_mode;
}

struct n2pc_header {
   uint32_t magic, version;
   uint8_t key[20];
   uint32_t payload_size;
   uint8_t payload_sha1[20];
};

static void
n2pc_key(const struct panfrost_uncompiled_shader *uncompiled, const struct panfrost_shader_key *shader_key, uint8_t out[20])
{
   struct mesa_sha1 ctx;
   const uint32_t sizes[3] = { (uint32_t)sizeof(struct panfrost_shader_key), (uint32_t)sizeof(struct pan_shader_info),
                               (uint32_t)sizeof(struct panfrost_sysvals) };
   _mesa_sha1_init(&ctx);
   _mesa_sha1_update(&ctx, N2PC_TAG, sizeof(N2PC_TAG));
   _mesa_sha1_update(&ctx, sizes, sizeof(sizes));
   const uint8_t push = (uint8_t)nord2_push_all_on();
   _mesa_sha1_update(&ctx, &push, 1);
   _mesa_sha1_update(&ctx, uncompiled->nir_sha1, sizeof(uncompiled->nir_sha1));
   _mesa_sha1_update(&ctx, shader_key, sizeof(*shader_key));
   _mesa_sha1_final(&ctx, out);
}

static void
n2pc_path_in(const char *dir, const uint8_t key[20], char *out, size_t size)
{
   char hex[41];
   _mesa_sha1_format(hex, key);
   snprintf(out, size, "%s/%s.pc", dir, hex);
}
#define n2pc_path(key, out, size) n2pc_path_in(n2pc_dir, (key), (out), (size))

void
panfrost_disk_cache_store(struct disk_cache *cache, const struct panfrost_uncompiled_shader *uncompiled,
                          const struct panfrost_shader_key *key, const struct panfrost_shader_binary *binary)
{
   (void)cache;
   if (n2pc_t_miss) {
      nord2_pancache_count[5] += n2pc_now_us() - n2pc_t_miss;
      n2pc_t_miss = 0;
   }
   if (n2pc_init() < 2 || nord2_pancache_count[2] >= N2PC_MAX_STORES || !binary->binary.size)
      return;
   struct blob blob;
   blob_init(&blob);
   blob_write_uint32(&blob, binary->binary.size);
   blob_write_bytes(&blob, binary->binary.data, binary->binary.size);
   blob_write_bytes(&blob, &binary->info, sizeof(binary->info));
   blob_write_bytes(&blob, &binary->sysvals, sizeof(binary->sysvals));
   if (blob.out_of_memory || blob.size + sizeof(struct n2pc_header) > N2PC_MAX_FILE) {
      blob_finish(&blob);
      return;
   }
   struct n2pc_header h = { .magic = N2PC_MAGIC, .version = N2PC_VERSION, .payload_size = (uint32_t)blob.size };
   n2pc_key(uncompiled, key, h.key);
   _mesa_sha1_compute(blob.data, blob.size, h.payload_sha1);
   char path[600], tmp[640];
   n2pc_path(h.key, path, sizeof(path));
   snprintf(tmp, sizeof(tmp), "%s.%d.tmp", path, (int)getpid());
   int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0644);
   bool ok = fd >= 0 && write(fd, &h, sizeof(h)) == (ssize_t)sizeof(h) && write(fd, blob.data, blob.size) == (ssize_t)blob.size;
   if (fd >= 0)
      ok = close(fd) == 0 && ok;
   if (ok)
      ok = rename(tmp, path) == 0;
   if (!ok) {
      if (fd >= 0)
         unlink(tmp);
      nord2_pancache_count[4]++;
   } else {
      nord2_pancache_count[2]++;
   }
   blob_finish(&blob);
}

bool
panfrost_disk_cache_retrieve(struct disk_cache *cache, const struct panfrost_uncompiled_shader *uncompiled,
                             const struct panfrost_shader_key *key, struct panfrost_shader_binary *binary)
{
   (void)cache;
   n2pc_t_miss = 0;
   if (n2pc_init() == 0)
      return false;
   uint64_t t0 = n2pc_now_us();
   uint8_t want[20];
   n2pc_key(uncompiled, key, want);
   char path[600];
   n2pc_path(want, path, sizeof(path));
   int fd = open(path, O_RDONLY);
   if (fd < 0 && n2pc_ro) {
      n2pc_path_in(N2PC_RO, want, path, sizeof(path));
      fd = open(path, O_RDONLY);
   }
   if (fd < 0) {
      nord2_pancache_count[1]++;
      n2pc_t_miss = n2pc_now_us();
      return false;
   }
   struct stat st;
   void *buf = NULL;
   struct n2pc_header h;
   bool ok = fstat(fd, &st) == 0 && st.st_size > (off_t)sizeof(h) && st.st_size <= (off_t)N2PC_MAX_FILE &&
             read(fd, &h, sizeof(h)) == (ssize_t)sizeof(h) && h.magic == N2PC_MAGIC && h.version == N2PC_VERSION &&
             !memcmp(h.key, want, sizeof(want)) && (off_t)(sizeof(h) + h.payload_size) == st.st_size &&
             (buf = malloc(h.payload_size)) != NULL && read(fd, buf, h.payload_size) == (ssize_t)h.payload_size;
   close(fd);
   if (ok) {
      uint8_t sum[20];
      _mesa_sha1_compute(buf, h.payload_size, sum);
      ok = !memcmp(sum, h.payload_sha1, sizeof(sum));
   }
   if (ok) {
      struct blob_reader blob;
      blob_reader_init(&blob, buf, h.payload_size);
      uint32_t binary_size = blob_read_uint32(&blob);
      ok = !blob.overrun && binary_size > 0 &&
           (size_t)binary_size + sizeof(binary->info) + sizeof(binary->sysvals) + 4 == h.payload_size;
      if (ok) {
         util_dynarray_init(&binary->binary, NULL);
         void *ptr = util_dynarray_resize_bytes(&binary->binary, binary_size, 1);
         ok = ptr != NULL;
         if (ok) {
            blob_copy_bytes(&blob, ptr, binary_size);
            blob_copy_bytes(&blob, &binary->info, sizeof(binary->info));
            blob_copy_bytes(&blob, &binary->sysvals, sizeof(binary->sysvals));
            ok = !blob.overrun;
         }
         if (!ok)
            util_dynarray_fini(&binary->binary);
      }
   }
   free(buf);
   if (!ok) {
      nord2_pancache_count[3]++;
      nord2_pancache_count[1]++;
      n2pc_t_miss = n2pc_now_us();
      return false;
   }
   nord2_pancache_count[0]++;
   nord2_pancache_count[6] += n2pc_now_us() - t0;
   return true;
}

void
panfrost_disk_cache_init(struct panfrost_screen *screen)
{
   screen->disk_cache = NULL;   /* (Mesa's own: not built) */
   (void)n2pc_init();
}
