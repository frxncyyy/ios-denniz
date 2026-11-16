/* gfxall3: pan_disk_cache.c (the file shader cache) on the host: round trip, key separation, every corruption rejected (never a wrong
 * binary), read-only and off modes, store cap. Built by pan-disk-cache-host-test.sh against the host Mesa build. */
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pan_context.h"
extern uint64_t nord2_pancache_count[8];
void panfrost_disk_cache_store(struct disk_cache *, const struct panfrost_uncompiled_shader *, const struct panfrost_shader_key *, const struct panfrost_shader_binary *);
bool panfrost_disk_cache_retrieve(struct disk_cache *, const struct panfrost_uncompiled_shader *, const struct panfrost_shader_key *, struct panfrost_shader_binary *);
#define CHECK(c, w) do { if (!(c)) { printf("FAIL %s (line %d)\n", w, __LINE__); exit(1); } } while (0)
static char *only_file(void)
{
   static char p[512]; DIR *d = opendir(N2PC_DIR); struct dirent *e; int n = 0;
   while ((e = readdir(d))) if (strstr(e->d_name, ".pc") && !strstr(e->d_name, "tmp")) { snprintf(p, sizeof(p), N2PC_DIR "/%s", e->d_name); n++; }
   closedir(d); return n == 1 ? p : NULL;
}
static void mkbin(struct panfrost_shader_binary *b, unsigned n, unsigned seed)
{
   memset(b, 0, sizeof(*b)); util_dynarray_init(&b->binary, NULL);
   uint8_t *p = util_dynarray_resize_bytes(&b->binary, n, 1); for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(i * 7 + seed);
   memset(&b->info, (int)seed, sizeof(b->info)); b->sysvals.sysval_count = seed;
}
int main(void)
{
   system("rm -rf " N2PC_DIR " && mkdir -p " N2PC_DIR);
   struct panfrost_uncompiled_shader u = { 0 }; struct panfrost_shader_key k = { 0 }, k2 = { 0 };
   for (int i = 0; i < 20; i++) u.nir_sha1[i] = (unsigned char)i;
   k2.fs.nr_cbufs_for_fragcolor = 1;
   struct panfrost_shader_binary in, out;
   CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out) && nord2_pancache_count[1] == 1, "empty: miss");
   mkbin(&in, 3000, 5); panfrost_disk_cache_store(NULL, &u, &k, &in);
   CHECK(nord2_pancache_count[2] == 1 && only_file(), "stored one file");
   CHECK(panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "hit");
   CHECK(out.binary.size == 3000 && !memcmp(out.binary.data, in.binary.data, 3000) && !memcmp(&out.info, &in.info, sizeof(in.info)) &&
         out.sysvals.sysval_count == 5, "identical");
   util_dynarray_fini(&out.binary);
   CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k2, &out), "other variant key: miss");
   u.nir_sha1[3] ^= 1; CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "other shader: miss"); u.nir_sha1[3] ^= 1;
   /* corruptions: every byte flip of the header and samples of the payload, truncation, extension -> rejected, never a hit */
   char *f = strdup(only_file()); struct stat st; stat(f, &st); size_t sz = st.st_size;
   uint8_t *orig = malloc(sz); FILE *fp = fopen(f, "rb"); fread(orig, 1, sz, fp); fclose(fp);
   unsigned rejected = 0;
   for (size_t pos = 0; pos < sz; pos += (pos < 64 ? 1 : 97)) {
      uint8_t *m = malloc(sz); memcpy(m, orig, sz); m[pos] ^= 0x40;
      fp = fopen(f, "wb"); fwrite(m, 1, sz, fp); fclose(fp); free(m);
      CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "corrupt byte must miss"); rejected++;
   }
   fp = fopen(f, "wb"); fwrite(orig, 1, sz - 1, fp); fclose(fp); CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "truncated");
   fp = fopen(f, "wb"); fwrite(orig, 1, sz, fp); fputc(0, fp); fclose(fp); CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "extended");
   /* a file of another key renamed onto this key's name */
   fp = fopen(f, "wb"); fwrite(orig, 1, sz, fp); fclose(fp);
   char g[600]; mkbin(&in, 100, 9); panfrost_disk_cache_store(NULL, &u, &k2, &in);
   { DIR *d = opendir(N2PC_DIR); struct dirent *e; while ((e = readdir(d))) if (strstr(e->d_name, ".pc") && strcmp(strrchr(f, '/') + 1, e->d_name)) snprintf(g, sizeof(g), N2PC_DIR "/%s", e->d_name); closedir(d); }
   rename(g, f); CHECK(!panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "foreign key under this name");
   CHECK(nord2_pancache_count[3] >= rejected + 3, "rejections counted");
   /* repaired by the next store, then a hit again */
   mkbin(&in, 3000, 5); panfrost_disk_cache_store(NULL, &u, &k, &in); CHECK(panfrost_disk_cache_retrieve(NULL, &u, &k, &out), "rewritten");
   util_dynarray_fini(&out.binary);
   printf("pan_disk_cache: ok (hits %llu misses %llu stored %llu rejected %llu, %u corruptions tried)\n",
          (unsigned long long)nord2_pancache_count[0], (unsigned long long)nord2_pancache_count[1],
          (unsigned long long)nord2_pancache_count[2], (unsigned long long)nord2_pancache_count[3], rejected + 3);
   return 0;
}
