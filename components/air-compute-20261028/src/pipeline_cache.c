/* See pipeline_cache.h.  Disk layout: <dir>/k<kind>-<slot>.tpc, slot = first
 * key bytes mod slots (a colliding key overwrites the slot; the stored full
 * key turns the loser's lookup into a miss).  Header (little endian):
 * "TGPC", u32 format, u32 kind, u32 0, key[32], u64 payload bytes,
 * sha256(payload)[32], build tag[8]; then the payload. */
#include "pipeline_cache.h"

extern void *malloc(size_t);
extern void free(void *);
extern void *memcpy(void *,const void *,size_t);
extern void *memset(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);
extern int strcmp(const char *,const char *);
extern int open(const char *,int,...);
extern long read(int,void *,size_t);
extern long write(int,const void *,size_t);
extern int close(int);
extern int fsync(int);
extern int rename(const char *,const char *);
extern int unlink(const char *);
extern int getpid(void);
extern int snprintf(char *,size_t,const char *,...);
extern void *dlopen(const char *,int);
extern void *dlsym(void *,const char *);
#ifdef __APPLE__
typedef long long tegu_pc_off;
extern int mkdir(const char *,unsigned short);
extern int fchmod(int,unsigned short);
extern int *__error(void);
#define TEGU_PC_ERRNO (*__error())
#define TEGU_PC_O_APPEND 0x8
#define TEGU_PC_O_CREAT 0x200
#define TEGU_PC_O_TRUNC 0x400
#define TEGU_PC_O_EXCL 0x800
#define TEGU_PC_O_CLOEXEC 0x1000000
#else
typedef long tegu_pc_off;
extern int mkdir(const char *,unsigned int);
extern int fchmod(int,unsigned int);
extern int *__errno_location(void);
#define TEGU_PC_ERRNO (*__errno_location())
#define TEGU_PC_O_APPEND 02000
#define TEGU_PC_O_CREAT 0100
#define TEGU_PC_O_TRUNC 01000
#define TEGU_PC_O_EXCL 0200
#define TEGU_PC_O_CLOEXEC 02000000
#endif
#define TEGU_PC_O_WRONLY 1
extern long pread(int,void *,size_t,tegu_pc_off);
extern tegu_pc_off lseek(int,tegu_pc_off,int);

static const uint32_t tegu_pc_k[64]={
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
static uint32_t tegu_pc_rotr(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void tegu_pc_block(uint32_t h[8],const unsigned char *p) {
 uint32_t w[64];
 for(unsigned i=0;i<16;i++)w[i]=(uint32_t)p[i*4]<<24|(uint32_t)p[i*4+1]<<16|(uint32_t)p[i*4+2]<<8|p[i*4+3];
 for(unsigned i=16;i<64;i++) {
  uint32_t x=w[i-15],y=w[i-2];
  w[i]=w[i-16]+(tegu_pc_rotr(x,7)^tegu_pc_rotr(x,18)^(x>>3))+w[i-7]+(tegu_pc_rotr(y,17)^tegu_pc_rotr(y,19)^(y>>10));
 }
 uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
 for(unsigned i=0;i<64;i++) {
  uint32_t t1=z+(tegu_pc_rotr(e,6)^tegu_pc_rotr(e,11)^tegu_pc_rotr(e,25))+((e&f)^(~e&g))+tegu_pc_k[i]+w[i];
  uint32_t t2=(tegu_pc_rotr(a,2)^tegu_pc_rotr(a,13)^tegu_pc_rotr(a,22))+((a&b)^(a&c)^(b&c));
  z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
 }
 h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
}
void tegu_pc_sha256_init(struct tegu_pc_sha256 *s) {
 static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 memcpy(s->h,iv,sizeof(iv));s->bytes=0;s->fill=0;
}
void tegu_pc_sha256_update(struct tegu_pc_sha256 *s,const void *data,size_t n) {
 const unsigned char *p=(const unsigned char *)data;
 if(!n)return;
 s->bytes+=n;
 if(s->fill) {
  size_t take=64-s->fill<n?64-s->fill:n;
  memcpy(s->block+s->fill,p,take);s->fill+=(unsigned)take;p+=take;n-=take;
  if(s->fill<64)return;
  tegu_pc_block(s->h,s->block);s->fill=0;
 }
 for(;n>=64;p+=64,n-=64)tegu_pc_block(s->h,p);
 if(n){memcpy(s->block,p,n);s->fill=(unsigned)n;}
}
void tegu_pc_sha256_final(struct tegu_pc_sha256 *s,unsigned char out[32]) {
 uint64_t bits=s->bytes*8;unsigned char pad[72];size_t n=(s->fill<56?56:120)-s->fill;
 memset(pad,0,sizeof(pad));pad[0]=0x80;
 for(unsigned i=0;i<8;i++)pad[n+i]=(unsigned char)(bits>>(56-8*i));
 uint64_t keep=s->bytes;tegu_pc_sha256_update(s,pad,n+8);s->bytes=keep;
 for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[i*4+j]=(unsigned char)(s->h[i]>>(24-8*j));
}
void tegu_pc_key_u32(struct tegu_pc_sha256 *s,uint32_t v) {
 unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),(unsigned char)(v>>16),(unsigned char)(v>>24)};
 tegu_pc_sha256_update(s,b,4);
}
void tegu_pc_key_bytes(struct tegu_pc_sha256 *s,const void *p,size_t n) {
 tegu_pc_key_u32(s,(uint32_t)n);tegu_pc_key_u32(s,(uint32_t)((uint64_t)n>>32));
 if(p&&n)tegu_pc_sha256_update(s,p,n);
}
void tegu_pc_key_str(struct tegu_pc_sha256 *s,const char *v) {
 size_t n=0;while(v&&v[n])n++;
 tegu_pc_key_bytes(s,v,n);
}

struct tegu_pc_entry {struct tegu_pc_entry *prev,*next;unsigned kind;unsigned char key[32];size_t bytes;unsigned char *data;};
static void tegu_pc_lock(struct tegu_pc *c){while(__atomic_exchange_n(&c->lock,1u,__ATOMIC_ACQUIRE)){}}
static void tegu_pc_unlock(struct tegu_pc *c){__atomic_store_n(&c->lock,0u,__ATOMIC_RELEASE);}
static void tegu_pc_unlink_entry(struct tegu_pc *c,struct tegu_pc_entry *e) {
 if(e->prev)e->prev->next=e->next;else c->head=e->next;
 if(e->next)e->next->prev=e->prev;else c->tail=e->prev;
 e->prev=e->next=0;
}
static void tegu_pc_push_front(struct tegu_pc *c,struct tegu_pc_entry *e) {
 e->prev=0;e->next=c->head;if(c->head)c->head->prev=e;c->head=e;if(!c->tail)c->tail=e;
}
static void tegu_pc_drop(struct tegu_pc *c,struct tegu_pc_entry *e) {
 tegu_pc_unlink_entry(c,e);c->memory_bytes[e->kind]-=e->bytes;c->stats.memory_bytes-=e->bytes;
 free(e->data);free(e);
}
static struct tegu_pc_entry *tegu_pc_find(struct tegu_pc *c,unsigned kind,const unsigned char key[32]) {
 for(struct tegu_pc_entry *e=c->head;e;e=e->next)if(e->kind==kind&&!memcmp(e->key,key,32))return e;
 return 0;
}
static void tegu_pc_memory_put(struct tegu_pc *c,unsigned kind,const unsigned char key[32],const void *p,size_t n) {
 const struct tegu_pc_kind_config *k=&c->kinds[kind];
 if(!k->memory_budget||n>k->memory_budget)return;
 unsigned char *copy=malloc(n?n:1);struct tegu_pc_entry *e=malloc(sizeof(*e));
 if(!copy||!e){free(copy);free(e);return;}
 memcpy(copy,p,n);memset(e,0,sizeof(*e));e->kind=kind;memcpy(e->key,key,32);e->bytes=n;e->data=copy;
 tegu_pc_lock(c);
 struct tegu_pc_entry *old=tegu_pc_find(c,kind,key);if(old)tegu_pc_drop(c,old);
 tegu_pc_push_front(c,e);c->memory_bytes[kind]+=n;c->stats.memory_bytes+=n;
 for(struct tegu_pc_entry *t=c->tail;c->memory_bytes[kind]>k->memory_budget&&t;) {
  struct tegu_pc_entry *prev=t->prev;if(t->kind==kind&&t!=e)tegu_pc_drop(c,t);t=prev;
 }
 tegu_pc_unlock(c);
}
void tegu_pc_build_tag(const char *build,unsigned char out[8]) {
 struct tegu_pc_sha256 s;unsigned char d[32];tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,build);tegu_pc_sha256_final(&s,d);
 memcpy(out,d,8);
}
void tegu_pc_init(struct tegu_pc *c,const char *dir,const char *build,const struct tegu_pc_kind_config kinds[TEGU_PC_KINDS]) {
 memset(c,0,sizeof(*c));c->pack_fd=-1;tegu_pc_build_tag(build?build:"",c->build_tag);
 for(unsigned i=0;i<TEGU_PC_KINDS;i++)c->kinds[i]=kinds[i];
 size_t n=0;while(dir&&dir[n]&&n<sizeof(c->dir)-1){c->dir[n]=dir[n];n++;}
 c->dir[n]=0;
 c->disk=n&&dir[n]==0&&dir[0]=='/'?0:-1;
}
static int tegu_pc_disk_ready(struct tegu_pc *c) {
 int state=__atomic_load_n(&c->disk,__ATOMIC_ACQUIRE);
 if(state)return state>0;
 /* Create the last path component; an existing directory is fine.  Whether
  * it is writable shows on the first store. */
 mkdir(c->dir,0755);
 __atomic_store_n(&c->disk,1,__ATOMIC_RELEASE);
 return 1;
}
int tegu_pc_slot_path(const struct tegu_pc *c,unsigned kind,const unsigned char key[32],char *out,size_t size) {
 if(kind==0||kind>=TEGU_PC_KINDS||!c->kinds[kind].slots||!c->dir[0])return -1;
 unsigned slot=(((unsigned)key[0]<<8)|key[1])%c->kinds[kind].slots;
 int n=snprintf(out,size,"%s/k%u-%05u.tpc",c->dir,kind,slot);
 return n>0&&(size_t)n<size?0:-1;
}
static void tegu_pc_put_u32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void tegu_pc_put_u64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t tegu_pc_get_u32(const unsigned char *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static uint64_t tegu_pc_get_u64(const unsigned char *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static int tegu_pc_read_all(int fd,unsigned char *p,size_t n) {
 while(n){long r=read(fd,p,n);if(r<=0)return -1;p+=r;n-=(size_t)r;}
 return 0;
}
static int tegu_pc_write_all(int fd,const unsigned char *p,size_t n) {
 while(n){long r=write(fd,p,n);if(r<=0)return -1;p+=r;n-=(size_t)r;}
 return 0;
}
/* 0 = valid entry read into *out (malloc'd), -1 = absent, -2 = present but
 * rejected (version, kind, key, length or checksum). */
static int tegu_pc_disk_read(struct tegu_pc *c,unsigned kind,const unsigned char key[32],unsigned char **out,size_t *bytes) {
 char path[600];if(tegu_pc_slot_path(c,kind,key,path,sizeof(path)))return -1;
 __atomic_fetch_add(&c->stats.disk_reads,1,__ATOMIC_RELAXED);
 int fd=open(path,TEGU_PC_O_CLOEXEC);if(fd<0){__atomic_fetch_add(&c->stats.disk_read_absent,1,__ATOMIC_RELAXED);return -1;}
 unsigned char h[TEGU_PC_HEADER_BYTES];int status=-2;unsigned char *data=0;
 if(tegu_pc_read_all(fd,h,sizeof(h)))goto done;
 if(memcmp(h,"TGPC",4)||tegu_pc_get_u32(h+4)!=TEGU_PC_FORMAT_VERSION||tegu_pc_get_u32(h+8)!=kind||tegu_pc_get_u32(h+12)||
    memcmp(h+88,c->build_tag,8))goto done;
 if(memcmp(h+16,key,32)){status=-1;goto done;} /* another key owns the slot */
 uint64_t n=tegu_pc_get_u64(h+48);
 if(n>c->kinds[kind].max_entry)goto done;
 data=malloc(n?(size_t)n:1);if(!data){status=-1;goto done;}
 if(tegu_pc_read_all(fd,data,(size_t)n))goto done;
 unsigned char extra;if(read(fd,&extra,1)!=0)goto done; /* trailing bytes */
 unsigned char sum[32];struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_sha256_update(&s,data,(size_t)n);tegu_pc_sha256_final(&s,sum);
 if(memcmp(sum,h+56,32))goto done;
 *out=data;*bytes=(size_t)n;data=0;status=0;
 __atomic_fetch_add(&c->stats.disk_bytes_read,n+sizeof(h),__ATOMIC_RELAXED);
done:
 free(data);close(fd);
 if(status==-2){unlink(path);__atomic_fetch_add(&c->stats.disk_rejects,1,__ATOMIC_RELAXED);}
 return status;
}
static int tegu_pc_pack_cmp(const struct tegu_pc_pack_entry *e,unsigned kind,const unsigned char key[32]) {
 if(e->kind!=kind)return e->kind<kind?-1:1;
 return memcmp(e->key,key,32);
}
/* zlib's uncompress, resolved on first use: libz is a system library on iOS, so the bundle links nothing. */
static int tegu_pc_inflate(const unsigned char *in,size_t inBytes,unsigned char *out,size_t outBytes) {
 static int (*volatile fn)(unsigned char *,unsigned long *,const unsigned char *,unsigned long);
 static volatile int tried;
 if(!fn&&!tried) {
#ifdef __APPLE__
  void *h=dlopen("/usr/lib/libz.1.dylib",2);
#else
  void *h=dlopen("libz.so.1",2);
#endif
  if(h)fn=(int (*)(unsigned char *,unsigned long *,const unsigned char *,unsigned long))dlsym(h,"uncompress");
  tried=1;
 }
 if(!fn)return -1;
 unsigned long n=(unsigned long)outBytes;
 return fn(out,&n,in,(unsigned long)inBytes)==0&&n==(unsigned long)outBytes?0:-1;
}
static int tegu_pc_pack_read(struct tegu_pc *c,unsigned kind,const unsigned char key[32],unsigned char **out,size_t *bytes) {
 if(c->pack_fd<0||!c->pack_count)return -1;
 unsigned lo=0,hi=c->pack_count;
 while(lo<hi){unsigned mid=lo+(hi-lo)/2;int r=tegu_pc_pack_cmp(&c->pack[mid],kind,key);if(!r){lo=mid;break;}if(r<0)lo=mid+1;else hi=mid;}
 if(lo>=c->pack_count||tegu_pc_pack_cmp(&c->pack[lo],kind,key))return -1;
 struct tegu_pc_pack_entry *e=&c->pack[lo];
 if(__atomic_load_n(&e->bad,__ATOMIC_RELAXED)||e->bytes>c->kinds[kind].max_entry)return -1;
 unsigned char *data=malloc(e->bytes?(size_t)e->bytes:1);if(!data)return -1;
 size_t done=0;
 while(done<e->bytes){long r=pread(c->pack_fd,data+done,(size_t)e->bytes-done,(tegu_pc_off)(e->offset+done));if(r<=0)break;done+=(size_t)r;}
 unsigned char sum[32];struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_sha256_update(&s,data,done);tegu_pc_sha256_final(&s,sum);
 if(done!=e->bytes||memcmp(sum,e->sha,32)) {
  free(data);__atomic_store_n(&e->bad,1u,__ATOMIC_RELAXED);
  __atomic_fetch_add(&c->stats.pack_rejects,1,__ATOMIC_RELAXED);return -1;
 }
 if(e->codec==TEGU_PC_PACK_ZLIB) {
  /* stored = u64 raw length, zlib stream (the checksum above covers the stored bytes) */
  uint64_t raw=done>=8?tegu_pc_get_u64(data):0;
  unsigned char *plain=raw&&raw<=c->kinds[kind].max_entry?malloc((size_t)raw):0;
  if(!plain||tegu_pc_inflate(data+8,done-8,plain,(size_t)raw)) {
   free(plain);free(data);__atomic_store_n(&e->bad,1u,__ATOMIC_RELAXED);
   __atomic_fetch_add(&c->stats.pack_rejects,1,__ATOMIC_RELAXED);return -1;
  }
  free(data);*out=plain;*bytes=(size_t)raw;return 0;
 }
 *out=data;*bytes=done;return 0;
}
int tegu_pc_attach_pack(struct tegu_pc *c,const char *path) {
 if(!path||c->pack_fd>=0)return -1;
 int fd=open(path,TEGU_PC_O_CLOEXEC);if(fd<0)return -2; /* -2: no such pack file */
 unsigned char h[32];struct tegu_pc_pack_entry *index=0;unsigned count=0;
 tegu_pc_off size=lseek(fd,0,2);
 if(size<(tegu_pc_off)sizeof(h)||pread(fd,h,sizeof(h),0)!=(long)sizeof(h))goto fail;
 count=tegu_pc_get_u32(h+8);
 if(memcmp(h,"TGPK",4)||tegu_pc_get_u32(h+4)!=TEGU_PC_PACK_VERSION||tegu_pc_get_u32(h+12)||
    memcmp(h+16,c->build_tag,8)||!count||count>TEGU_PC_PACK_MAX_ENTRIES)goto fail;
 size_t indexBytes=(size_t)count*88;unsigned char *raw=malloc(indexBytes);
 index=malloc(count*sizeof(*index));
 if(!raw||!index||pread(fd,raw,indexBytes,sizeof(h))!=(long)indexBytes){free(raw);goto fail;}
 for(unsigned i=0;i<count;i++) {
  const unsigned char *r=raw+(size_t)i*88;struct tegu_pc_pack_entry *e=&index[i];
  e->kind=tegu_pc_get_u32(r);e->bad=0;e->codec=tegu_pc_get_u32(r+4);memcpy(e->key,r+8,32);e->offset=tegu_pc_get_u64(r+40);e->bytes=tegu_pc_get_u64(r+48);
  memcpy(e->sha,r+56,32);
  int ordered=!i||tegu_pc_pack_cmp(&index[i-1],e->kind,e->key)<0;
  if(e->codec>TEGU_PC_PACK_ZLIB||!e->kind||e->kind>=TEGU_PC_KINDS||!ordered||e->offset<sizeof(h)+indexBytes||
     e->bytes>(uint64_t)size||e->offset>(uint64_t)size-e->bytes){free(raw);goto fail;}
 }
 free(raw);c->pack=index;c->pack_count=count;c->pack_fd=fd;return 0;
fail:
 free(index);close(fd);return -1;
}
void tegu_pc_append_record(struct tegu_pc *c,const char *name,const char *text,size_t bytes,size_t max) {
 if(__atomic_load_n(&c->disk,__ATOMIC_ACQUIRE)<=0||!text||!bytes)return;
 char path[600];int n=snprintf(path,sizeof(path),"%s/%s",c->dir,name);if(n<=0||(size_t)n>=sizeof(path))return;
 int fd=open(path,TEGU_PC_O_WRONLY|TEGU_PC_O_CREAT|TEGU_PC_O_APPEND|TEGU_PC_O_CLOEXEC,0644);if(fd<0)return;
 tegu_pc_off at=lseek(fd,0,2);
 if(at>=0&&(uint64_t)at+bytes<=max)tegu_pc_write_all(fd,(const unsigned char *)text,bytes);
 close(fd);
}
void tegu_pc_close(struct tegu_pc *c) {
 tegu_pc_clear_memory(c);
 if(c->pack_fd>=0)close(c->pack_fd);
 free(c->pack);c->pack=0;c->pack_count=0;c->pack_fd=-1;
}
int tegu_pc_lookup(struct tegu_pc *c,unsigned kind,const unsigned char key[32],unsigned char **payload,size_t *bytes) {
 *payload=0;*bytes=0;
 if(kind==0||kind>=TEGU_PC_KINDS)return 0;
 tegu_pc_lock(c);
 struct tegu_pc_entry *e=tegu_pc_find(c,kind,key);
 if(e) {
  unsigned char *copy=malloc(e->bytes?e->bytes:1);
  if(copy) {
   memcpy(copy,e->data,e->bytes);*payload=copy;*bytes=e->bytes;
   tegu_pc_unlink_entry(c,e);tegu_pc_push_front(c,e);c->stats.memory_hits++;
   tegu_pc_unlock(c);return 1;
  }
 }
 tegu_pc_unlock(c);
 if(c->kinds[kind].slots&&tegu_pc_disk_ready(c)&&!tegu_pc_disk_read(c,kind,key,payload,bytes)) {
  tegu_pc_memory_put(c,kind,key,*payload,*bytes);
  __atomic_fetch_add(&c->stats.disk_hits,1,__ATOMIC_RELAXED);return 2;
 }
 if(!tegu_pc_pack_read(c,kind,key,payload,bytes)) {
  tegu_pc_memory_put(c,kind,key,*payload,*bytes);
  __atomic_fetch_add(&c->stats.pack_hits,1,__ATOMIC_RELAXED);return 3;
 }
 __atomic_fetch_add(&c->stats.misses,1,__ATOMIC_RELAXED);
 return 0;
}
void tegu_pc_store(struct tegu_pc *c,unsigned kind,const unsigned char key[32],const void *payload,size_t bytes) {
 if(kind==0||kind>=TEGU_PC_KINDS||(!payload&&bytes))return;
 __atomic_fetch_add(&c->stats.stores,1,__ATOMIC_RELAXED);
 tegu_pc_memory_put(c,kind,key,payload,bytes);
 if(!c->kinds[kind].slots||bytes>c->kinds[kind].max_entry||!tegu_pc_disk_ready(c))return;
 char path[600],tmp[640];if(tegu_pc_slot_path(c,kind,key,path,sizeof(path)))return;
 unsigned serial=__atomic_fetch_add(&c->tmp_serial,1,__ATOMIC_RELAXED);
 int n=snprintf(tmp,sizeof(tmp),"%s/.tmp-%d-%u",c->dir,getpid(),serial);
 if(n<=0||(size_t)n>=sizeof(tmp))return;
 unsigned char h[TEGU_PC_HEADER_BYTES];memset(h,0,sizeof(h));
 memcpy(h,"TGPC",4);tegu_pc_put_u32(h+4,TEGU_PC_FORMAT_VERSION);tegu_pc_put_u32(h+8,kind);
 memcpy(h+16,key,32);tegu_pc_put_u64(h+48,bytes);memcpy(h+88,c->build_tag,8);
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_sha256_update(&s,payload,bytes);tegu_pc_sha256_final(&s,h+56);
 int fd=open(tmp,TEGU_PC_O_WRONLY|TEGU_PC_O_CREAT|TEGU_PC_O_EXCL|TEGU_PC_O_TRUNC|TEGU_PC_O_CLOEXEC,0644);
 int err=fd<0?TEGU_PC_ERRNO:0;
 /* Entries are shared by every process of the build (root probe, mobile UI
  * processes): never inherit a restrictive umask. */
 if(fd>=0)fchmod(fd,0644);
 int ok=fd>=0&&!tegu_pc_write_all(fd,h,sizeof(h))&&!tegu_pc_write_all(fd,(const unsigned char *)payload,bytes);
 /* fsync: the USB hotfix reboot does not sync, and R199 lost entries
  * written in its last minutes. */
 if(ok&&fsync(fd)){ok=0;err=TEGU_PC_ERRNO;}
 if(fd>=0&&close(fd)){ok=0;err=TEGU_PC_ERRNO;}
 if(ok&&rename(tmp,path)){ok=0;err=TEGU_PC_ERRNO;}
 if(!ok) {
  if(!err)err=TEGU_PC_ERRNO;
  if(fd>=0)unlink(tmp);
  __atomic_fetch_add(&c->stats.disk_write_failures,1,__ATOMIC_RELAXED);
  __atomic_store_n(&c->stats.last_write_errno,(uint64_t)(unsigned)err,__ATOMIC_RELAXED);
  /* An unwritable directory stops disk use after a few attempts. */
  if(__atomic_add_fetch(&c->write_failures,1,__ATOMIC_RELAXED)==4) {
   __atomic_store_n(&c->disk,-1,__ATOMIC_RELEASE);
   char line[700];int m=snprintf(line,sizeof(line),"TEGU_PIPELINE_CACHE disk_disabled after 4 write failures errno=%d dir=%.400s\n",err,c->dir);
   if(m>0)write(2,line,(size_t)m<sizeof(line)?(size_t)m:sizeof(line)-1);
  }
  return;
 }
 __atomic_fetch_add(&c->stats.disk_writes,1,__ATOMIC_RELAXED);
 __atomic_fetch_add(&c->stats.disk_bytes_written,bytes+sizeof(h),__ATOMIC_RELAXED);
}
int tegu_pc_dir_check(const char *dir,struct tegu_pc_dir_diag *diag) {
 struct tegu_pc_dir_diag local;if(!diag)diag=&local;
 diag->mkdir_errno=diag->open_errno=diag->write_errno=0;
 if(!dir||dir[0]!='/'){diag->open_errno=22;return 0;}
 /* Shared by processes of different uids: 0755 (umask may only narrow it). */
 if(mkdir(dir,0755)&&TEGU_PC_ERRNO!=17)diag->mkdir_errno=TEGU_PC_ERRNO; /* 17 = EEXIST */
 char probe[640];int n=snprintf(probe,sizeof(probe),"%s/.probe-%d",dir,getpid());
 if(n<=0||(size_t)n>=sizeof(probe)){diag->open_errno=36;return 0;}
 int fd=open(probe,TEGU_PC_O_WRONLY|TEGU_PC_O_CREAT|TEGU_PC_O_TRUNC|TEGU_PC_O_CLOEXEC,0644);
 if(fd<0){diag->open_errno=TEGU_PC_ERRNO;return 0;}
 int ok=write(fd,"x",1)==1;if(!ok)diag->write_errno=TEGU_PC_ERRNO;
 close(fd);unlink(probe);
 return ok;
}
int tegu_pc_dir_usable(const char *dir) {return tegu_pc_dir_check(dir,0);}
int tegu_pc_choose_dir(const struct tegu_pc_candidate *candidates,unsigned count,const char *leaf,
 char *out,size_t outSize,char *diag,size_t diagSize) {
 size_t used=0;if(out&&outSize)out[0]=0;if(diag&&diagSize)diag[0]=0;
 if(!candidates||!leaf||!out||!outSize)return -1;
 char tried[8][512];unsigned triedCount=0;
 for(unsigned i=0;i<count;i++) {
  const char *base=candidates[i].base,*suffix=candidates[i].suffix?candidates[i].suffix:"";
  char path[512];
  int n=base&&base[0]=='/'?snprintf(path,sizeof(path),"%s%s/%s",base,suffix,leaf):-1;
  int entry=0;char text[600];
  if(n<=0||(size_t)n>=sizeof(path))entry=snprintf(text,sizeof(text),"%s=unset",base&&base[0]?base:"-");
  else {
   int seen=0;for(unsigned k=0;k<triedCount;k++)if(!strcmp(tried[k],path))seen=1;
   if(seen)continue;
   if(triedCount<8){memcpy(tried[triedCount],path,(size_t)n+1);triedCount++;}
   struct tegu_pc_dir_diag d;
   if(tegu_pc_dir_check(path,&d)) {
    entry=snprintf(text,sizeof(text),"%s=ok",path);
    if(diag&&diagSize&&entry>0&&used+(size_t)entry+2<diagSize){
     if(used)diag[used++]=';';
     memcpy(diag+used,text,(size_t)entry);used+=(size_t)entry;diag[used]=0;
    }
    memcpy(out,path,(size_t)n+1>outSize?outSize:(size_t)n+1);out[outSize-1]=0;
    return (int)i;
   }
   entry=snprintf(text,sizeof(text),"%s=mkdir:%d,open:%d,write:%d",path,d.mkdir_errno,d.open_errno,d.write_errno);
  }
  if(diag&&diagSize&&entry>0&&(size_t)entry<sizeof(text)&&used+(size_t)entry+2<diagSize){
   if(used)diag[used++]=';';
   memcpy(diag+used,text,(size_t)entry);used+=(size_t)entry;diag[used]=0;
  }
 }
 return -1;
}
void tegu_pc_reject(struct tegu_pc *c,unsigned kind,const unsigned char key[32]) {
 if(kind==0||kind>=TEGU_PC_KINDS)return;
 __atomic_fetch_add(&c->stats.decode_rejects,1,__ATOMIC_RELAXED);
 tegu_pc_lock(c);
 struct tegu_pc_entry *e=tegu_pc_find(c,kind,key);if(e)tegu_pc_drop(c,e);
 tegu_pc_unlock(c);
 char path[600];
 if(__atomic_load_n(&c->disk,__ATOMIC_ACQUIRE)>0&&!tegu_pc_slot_path(c,kind,key,path,sizeof(path))) {
  /* Only remove the slot when it still holds this key. */
  unsigned char *data=0;size_t n=0;
  if(!tegu_pc_disk_read(c,kind,key,&data,&n)){free(data);unlink(path);}
 }
}
void tegu_pc_clear_memory(struct tegu_pc *c) {
 tegu_pc_lock(c);
 while(c->head)tegu_pc_drop(c,c->head);
 tegu_pc_unlock(c);
}
void tegu_pc_snapshot(struct tegu_pc *c,struct tegu_pc_stats *out) {
 tegu_pc_lock(c);*out=c->stats;tegu_pc_unlock(c);
}
