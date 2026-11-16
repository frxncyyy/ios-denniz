/* Render pipeline compile cache: content-keyed blobs in memory (LRU, byte
 * budget per kind) and on disk (one file per hashed slot, bounded count and
 * entry size).  The caller owns the key (SHA-256 over everything that affects
 * the compiled output, including the compiler build) and the payload format.
 * Every disk entry carries a format version, its kind, its full key, the
 * payload length and a SHA-256 of the payload; any mismatch or I/O error is a
 * miss.  Freestanding: no system headers beyond stddef/stdint. */
#ifndef TEGU_PIPELINE_CACHE_H
#define TEGU_PIPELINE_CACHE_H
#include <stddef.h>
#include <stdint.h>

#define TEGU_PC_FORMAT_VERSION 2u
#define TEGU_PC_PACK_VERSION 1u
#define TEGU_PC_PACK_MAX_ENTRIES 8192u
#define TEGU_PC_KINDS 3u           /* 1 = AIR translation, 2 = Mali pair */
#define TEGU_PC_KIND_AIR 1u
#define TEGU_PC_KIND_MALI 2u
#define TEGU_PC_HEADER_BYTES 96u

struct tegu_pc_sha256 {uint32_t h[8];uint64_t bytes;unsigned char block[64];unsigned fill;};
void tegu_pc_sha256_init(struct tegu_pc_sha256 *);
void tegu_pc_sha256_update(struct tegu_pc_sha256 *,const void *,size_t);
void tegu_pc_sha256_final(struct tegu_pc_sha256 *,unsigned char out[32]);
/* Unambiguous key material: a length prefix before variable-length fields. */
void tegu_pc_key_u32(struct tegu_pc_sha256 *,uint32_t);
void tegu_pc_key_bytes(struct tegu_pc_sha256 *,const void *,size_t);
void tegu_pc_key_str(struct tegu_pc_sha256 *,const char *);

struct tegu_pc_entry;
struct tegu_pc_kind_config {size_t memory_budget,max_entry;unsigned slots;};
struct tegu_pc_stats {uint64_t memory_hits,disk_hits,misses,stores,disk_writes,disk_rejects,
 decode_rejects,disk_bytes_read,disk_bytes_written,memory_bytes,pack_hits,pack_rejects,
 disk_reads,disk_read_absent,disk_write_failures,last_write_errno;};
/* Read-only pack of entries (made from cache files by make_pipeline_pack.py)
 * consulted after the disk: header "TGPK", u32 version, u32 count, u32 0,
 * build tag[8], 8 zero bytes; then count index records {u32 kind, u32 codec (0 raw, 1 zlib),
 * key[32], u64 offset, u64 bytes, sha256(payload)[32]} sorted by (kind,key);
 * then the payloads. */
#define TEGU_PC_PACK_RAW 0u
#define TEGU_PC_PACK_ZLIB 1u  /* stored = u64 raw length + zlib stream (libz uncompress, resolved with dlsym) */
struct tegu_pc_pack_entry {uint32_t kind,bad,codec;unsigned char key[32];uint64_t offset,bytes;unsigned char sha[32];};
struct tegu_pc {
 unsigned lock;
 int disk;                 /* 0 untried, 1 usable, -1 disabled */
 unsigned write_failures,tmp_serial;
 char dir[512];
 unsigned char build_tag[8];      /* sha256(build id)[0..8], in every file */
 int pack_fd;unsigned pack_count;struct tegu_pc_pack_entry *pack;
 struct tegu_pc_kind_config kinds[TEGU_PC_KINDS];
 struct tegu_pc_entry *head,*tail; /* most recent first */
 size_t memory_bytes[TEGU_PC_KINDS];
 struct tegu_pc_stats stats;
};
/* dir: absolute directory that exists or can be created (mkdir -p of the last
 * component only), or NULL/"" for memory only. */
void tegu_pc_init(struct tegu_pc *,const char *dir,const char *build,const struct tegu_pc_kind_config kinds[TEGU_PC_KINDS]);
/* Attach a pack file; refused (returns -1, no pack) unless its version and
 * build tag match and every index record lies inside the file; -2 when the
 * file does not exist or cannot be opened (the usual cause on device: the
 * bundle shipped without a pack). */
int tegu_pc_attach_pack(struct tegu_pc *,const char *path);
/* Release memory entries and the pack. */
void tegu_pc_close(struct tegu_pc *);
/* Build tag of a build id string (first 8 bytes of its SHA-256). */
void tegu_pc_build_tag(const char *build,unsigned char out[8]);
/* Append one text record to <dir>/name, keeping the file under max bytes. */
void tegu_pc_append_record(struct tegu_pc *,const char *name,const char *text,size_t bytes,size_t max);
/* 1 = memory hit, 2 = disk hit, 3 = pack hit (both promoted to memory), 0 = miss.  On a hit
 * *payload is a malloc'd copy the caller frees. */
int tegu_pc_lookup(struct tegu_pc *,unsigned kind,const unsigned char key[32],unsigned char **payload,size_t *bytes);
/* Store in memory and, when enabled, on disk (atomic rename). */
void tegu_pc_store(struct tegu_pc *,unsigned kind,const unsigned char key[32],const void *payload,size_t bytes);
/* The caller could not decode a payload it got from lookup: forget it in
 * memory and on disk so the next build compiles and replaces it. */
void tegu_pc_reject(struct tegu_pc *,unsigned kind,const unsigned char key[32]);
/* Drop every in-memory entry (tests; disk is untouched). */
void tegu_pc_clear_memory(struct tegu_pc *);
void tegu_pc_snapshot(struct tegu_pc *,struct tegu_pc_stats *);
/* Create dir (last component only) and prove it writable with a probe file.
 * 1 = usable. */
int tegu_pc_dir_usable(const char *dir);
/* Same, reporting why it failed: errno of mkdir (EEXIST is not an error), of
 * the probe open and of the probe write; all 0 when usable. */
struct tegu_pc_dir_diag {int mkdir_errno,open_errno,write_errno;};
int tegu_pc_dir_check(const char *dir,struct tegu_pc_dir_diag *diag);
/* Directory selection: the first candidate whose <base><suffix>/<leaf> passes
 * tegu_pc_dir_check.  A NULL or relative base is skipped ("unset"), a path
 * already tried is not tried twice.  diag receives one entry per candidate
 * ("<path>=ok" or "<path>=mkdir:E,open:E,write:E", ';' separated) for the
 * per-process log line.  Returns the chosen index (out holds the path) or -1
 * (out empty). */
struct tegu_pc_candidate {const char *base,*suffix;};
int tegu_pc_choose_dir(const struct tegu_pc_candidate *candidates,unsigned count,const char *leaf,
 char *out,size_t outSize,char *diag,size_t diagSize);
/* Slot file path of (kind,key); for tests. Returns 0 on success. */
int tegu_pc_slot_path(const struct tegu_pc *,unsigned kind,const unsigned char key[32],char *out,size_t size);
#endif
