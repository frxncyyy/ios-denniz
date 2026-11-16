/* Host test of pipeline_cache.c: SHA-256 vectors, memory/disk hit and miss,
 * corrupt, truncated, version-changed and foreign-kind files, slot
 * collisions, size bounds, LRU budget and an unwritable directory.
 * cc -O1 -fsanitize=address,undefined pipeline_cache_host_test.c pipeline_cache.c */
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "pipeline_cache.h"

static void hex(const unsigned char d[32],char out[65]) {
 for(unsigned i=0;i<32;i++)sprintf(out+2*i,"%02x",d[i]);
}
static void sha(const void *p,size_t n,char out[65]) {
 struct tegu_pc_sha256 s;unsigned char d[32];tegu_pc_sha256_init(&s);
 /* Uneven chunks exercise the buffered update path. */
 const unsigned char *b=p;size_t step=1;
 while(n){size_t k=step<n?step:n;tegu_pc_sha256_update(&s,b,k);b+=k;n-=k;step=step*3+1;}
 tegu_pc_sha256_final(&s,d);hex(d,out);
}
static void key_of(const char *name,unsigned char key[32]) {
 struct tegu_pc_sha256 s;tegu_pc_sha256_init(&s);tegu_pc_key_str(&s,name);tegu_pc_sha256_final(&s,key);
}
static long file_size(const char *path) {
 struct stat st;return stat(path,&st)?-1:(long)st.st_size;
}
static void patch_byte(const char *path,long at,unsigned char xor) {
 FILE *f=fopen(path,"r+b");assert(f);assert(!fseek(f,at,SEEK_SET));int c=fgetc(f);assert(c!=EOF);
 assert(!fseek(f,at,SEEK_SET));fputc(c^xor,f);fclose(f);
}
static void reinit(struct tegu_pc *c,const char *dir,const struct tegu_pc_kind_config *kinds) {
 tegu_pc_close(c);tegu_pc_init(c,dir,"build-A",kinds);
}
int main(int argc,char **argv) {
 char out[65];
 sha("abc",3,out);assert(!strcmp(out,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
 sha("",0,out);assert(!strcmp(out,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
 static unsigned char million[1000000];memset(million,'a',sizeof(million));
 sha(million,sizeof(million),out);assert(!strcmp(out,"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
 const char *msg="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
 sha(msg,strlen(msg),out);assert(!strcmp(out,"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
 printf("PASS pipeline cache SHA-256 vectors (abc, empty, 1M a, 448-bit)\n");

 char dir[512];snprintf(dir,sizeof(dir),"%s/tpc",argc>1?argv[1]:"/tmp");
 struct tegu_pc_kind_config kinds[TEGU_PC_KINDS]={{0,0,0},{4096,8192,4},{1024,600,64}};
 static struct tegu_pc c;tegu_pc_init(&c,dir,"build-A",kinds);
 unsigned char k1[32],k2[32],*p=0;size_t n=0;key_of("one",k1);key_of("two",k2);
 unsigned char payload[700];for(unsigned i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)(i*7+1);

 /* Miss, store, memory hit, disk hit after the memory is gone. */
 assert(tegu_pc_lookup(&c,1,k1,&p,&n)==0&&!p);
 tegu_pc_store(&c,1,k1,payload,500);
 assert(tegu_pc_lookup(&c,1,k1,&p,&n)==1&&n==500&&!memcmp(p,payload,500));free(p);
 char path[600];assert(!tegu_pc_slot_path(&c,1,k1,path,sizeof(path)));
 assert(file_size(path)==TEGU_PC_HEADER_BYTES+500);
 tegu_pc_clear_memory(&c);
 assert(tegu_pc_lookup(&c,1,k1,&p,&n)==2&&n==500&&!memcmp(p,payload,500));free(p);
 assert(tegu_pc_lookup(&c,1,k1,&p,&n)==1);free(p); /* promoted */
 /* Same key, other kind: miss. */
 assert(tegu_pc_lookup(&c,2,k1,&p,&n)==0);
 /* A fresh cache object (next boot) reads the disk entry. */
 static struct tegu_pc next;reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==2&&n==500&&!memcmp(p,payload,500));free(p);
 printf("PASS pipeline cache miss, store, memory hit, disk hit, next-process disk hit\n");

 /* Corrupt payload byte: checksum miss, file removed, then recompile+store works. */
 struct tegu_pc_stats st;
 reinit(&next,dir,kinds);patch_byte(path,TEGU_PC_HEADER_BYTES+17,0x40);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&!p&&file_size(path)<0);
 tegu_pc_snapshot(&next,&st);assert(st.disk_rejects==1&&st.misses==1);
 tegu_pc_store(&next,1,k1,payload,500);reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==2);free(p);
 /* Corrupt checksum field and the length field. */
 reinit(&next,dir,kinds);patch_byte(path,60,1);assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 tegu_pc_store(&next,1,k1,payload,500);reinit(&next,dir,kinds);patch_byte(path,48,2);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 /* Truncated file and trailing garbage. */
 tegu_pc_store(&next,1,k1,payload,500);assert(!truncate(path,TEGU_PC_HEADER_BYTES+499));reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 tegu_pc_store(&next,1,k1,payload,500);{FILE *f=fopen(path,"ab");fputc(0,f);fclose(f);}reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 /* Header shorter than the fixed header. */
 tegu_pc_store(&next,1,k1,payload,500);assert(!truncate(path,20));reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 printf("PASS pipeline cache corrupt payload, checksum, length, truncated, trailing and short files miss and are removed\n");

 /* Format version change and foreign kind field: miss and removal. */
 tegu_pc_store(&next,1,k1,payload,500);reinit(&next,dir,kinds);patch_byte(path,4,TEGU_PC_FORMAT_VERSION^(TEGU_PC_FORMAT_VERSION+1));
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 tegu_pc_store(&next,1,k1,payload,500);reinit(&next,dir,kinds);patch_byte(path,8,3);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0&&file_size(path)<0);
 /* A key that differs only in content of the key (compiler build changed):
  * the old file stays a miss for the new key and is never served. */
 unsigned char k1b[32];memcpy(k1b,k1,32);k1b[31]^=1; /* same slot (first bytes equal) */
 tegu_pc_store(&next,1,k1,payload,500);reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1b,&p,&n)==0&&file_size(path)==TEGU_PC_HEADER_BYTES+500);
 /* The colliding key takes the slot; the first key then misses. */
 tegu_pc_store(&next,1,k1b,payload+1,300);reinit(&next,dir,kinds);
 assert(tegu_pc_lookup(&next,1,k1,&p,&n)==0);
 assert(tegu_pc_lookup(&next,1,k1b,&p,&n)==2&&n==300&&!memcmp(p,payload+1,300));free(p);
 printf("PASS pipeline cache version change, foreign kind, changed key and slot collision miss\n");

 /* Size bounds: disk entries above max_entry are not written; memory LRU
  * keeps the kind's budget. */
 reinit(&next,dir,kinds);unsigned char k3[32];key_of("big",k3);
 tegu_pc_store(&next,2,k3,payload,601);assert(!tegu_pc_slot_path(&next,2,k3,path,sizeof(path))&&file_size(path)<0);
 assert(tegu_pc_lookup(&next,2,k3,&p,&n)==1);free(p); /* fits the 1024 B memory budget */
 unsigned char k4[32];key_of("lru",k4);tegu_pc_store(&next,2,k4,payload,600);
 tegu_pc_snapshot(&next,&st);assert(st.memory_bytes==600);
 assert(tegu_pc_lookup(&next,2,k3,&p,&n)==0);  /* evicted, never on disk */
 tegu_pc_clear_memory(&next);
 assert(tegu_pc_lookup(&next,2,k4,&p,&n)==2&&n==600);free(p);
 /* Memory-only cache. */
 static struct tegu_pc mem;tegu_pc_init(&mem,0,"build-A",kinds);
 tegu_pc_store(&mem,1,k2,payload,10);assert(tegu_pc_lookup(&mem,1,k2,&p,&n)==1);free(p);
 tegu_pc_clear_memory(&mem);assert(tegu_pc_lookup(&mem,1,k2,&p,&n)==0);
 /* Reject drops memory and disk. */
 tegu_pc_store(&next,1,k2,payload,64);assert(!tegu_pc_slot_path(&next,1,k2,path,sizeof(path))&&file_size(path)>0);
 tegu_pc_reject(&next,1,k2);assert(file_size(path)<0&&tegu_pc_lookup(&next,1,k2,&p,&n)==0);
 printf("PASS pipeline cache disk entry bound, memory LRU budget, memory-only mode, reject\n");

 /* Unwritable directory: stores keep working in memory, disk use stops. */
 char ro[600];snprintf(ro,sizeof(ro),"%s/readonly",dir);mkdir(ro,0500);
 static struct tegu_pc bad;char sub[700];snprintf(sub,sizeof(sub),"%s/missing/deeper",ro);
 tegu_pc_init(&bad,sub,"build-A",kinds);
 for(unsigned i=0;i<6;i++){unsigned char k[32];char name[16];snprintf(name,sizeof(name),"w%u",i);key_of(name,k);tegu_pc_store(&bad,1,k,payload,32);}
 tegu_pc_snapshot(&bad,&st);assert(st.disk_writes==0&&bad.disk==-1);
 unsigned char kw[32];key_of("w5",kw);assert(tegu_pc_lookup(&bad,1,kw,&p,&n)==1);free(p);
 assert(!tegu_pc_dir_usable(sub)&&!tegu_pc_dir_usable("relative")&&tegu_pc_dir_usable(dir));
 static struct tegu_pc rel;tegu_pc_init(&rel,"relative/dir","build-A",kinds);assert(rel.disk==-1);
 printf("PASS pipeline cache unwritable directory disables disk, relative path refused\n");
 /* Another compiler build: its files are rejected (and removed), never served. */
 reinit(&next,dir,kinds);tegu_pc_store(&next,1,k2,payload,64);
 static struct tegu_pc other;tegu_pc_init(&other,dir,"build-B",kinds);
 assert(tegu_pc_lookup(&other,1,k2,&p,&n)==0);tegu_pc_snapshot(&other,&st);assert(st.disk_rejects==1);
 assert(!tegu_pc_slot_path(&next,1,k2,path,sizeof(path))&&file_size(path)<0);
 printf("PASS pipeline cache build tag change rejects old files\n");

 /* Pack: valid, payload corrupt, wrong build, bad index, out-of-range. */
 char pack[600];snprintf(pack,sizeof(pack),"%s/test.tpcpack",dir);
 unsigned char tag[8];tegu_pc_build_tag("build-A",tag);
 unsigned char ka[32],kb[32];key_of("pa",ka);key_of("pb",kb);
 const unsigned char *keys[2]={ka,kb};if(memcmp(ka,kb,32)>0){keys[0]=kb;keys[1]=ka;}
 const size_t sizes[2]={300,120};
 #define WRITE_PACK(TAG,BREAK) do { \
  FILE *pf=fopen(pack,"wb");assert(pf);unsigned char hdr[32]={'T','G','P','K',1,0,0,0,2,0,0,0};memcpy(hdr+16,TAG,8); \
  fwrite(hdr,1,32,pf);uint64_t off=32+2*88; \
  for(unsigned i=0;i<2;i++){unsigned char r[88]={0};r[0]=1;memcpy(r+8,keys[i],32);uint64_t o=off+(BREAK==3&&i?100000:0); \
   for(unsigned b=0;b<8;b++){r[40+b]=(unsigned char)(o>>(8*b));r[48+b]=(unsigned char)((uint64_t)sizes[i]>>(8*b));} \
   struct tegu_pc_sha256 hs;tegu_pc_sha256_init(&hs);tegu_pc_sha256_update(&hs,payload+i,sizes[i]);tegu_pc_sha256_final(&hs,r+56); \
   if(BREAK==2&&i)r[0]=7;fwrite(r,1,88,pf);off+=sizes[i];} \
  for(unsigned i=0;i<2;i++){unsigned char tmp[300];memcpy(tmp,payload+i,sizes[i]);if(BREAK==1&&i)tmp[5]^=1;fwrite(tmp,1,sizes[i],pf);} \
  fclose(pf);}while(0)
 static struct tegu_pc pk;
 WRITE_PACK(tag,0);tegu_pc_init(&pk,0,"build-A",kinds);assert(!tegu_pc_attach_pack(&pk,pack));
 assert(tegu_pc_lookup(&pk,1,keys[0],&p,&n)==3&&n==300&&!memcmp(p,payload,300));free(p);
 assert(tegu_pc_lookup(&pk,1,keys[0],&p,&n)==1);free(p); /* promoted to memory */
 assert(tegu_pc_lookup(&pk,1,keys[1],&p,&n)==3&&n==120&&!memcmp(p,payload+1,120));free(p);
 assert(tegu_pc_lookup(&pk,2,keys[0],&p,&n)==0&&tegu_pc_lookup(&pk,1,k1,&p,&n)==0);
 tegu_pc_close(&pk);
 WRITE_PACK(tag,1);tegu_pc_init(&pk,0,"build-A",kinds);assert(!tegu_pc_attach_pack(&pk,pack));
 assert(tegu_pc_lookup(&pk,1,keys[1],&p,&n)==0);tegu_pc_snapshot(&pk,&st);assert(st.pack_rejects==1);
 assert(tegu_pc_lookup(&pk,1,keys[0],&p,&n)==3);free(p);tegu_pc_close(&pk);
 tegu_pc_init(&pk,0,"build-B",kinds);assert(tegu_pc_attach_pack(&pk,pack)&&tegu_pc_lookup(&pk,1,keys[0],&p,&n)==0);tegu_pc_close(&pk);
 WRITE_PACK(tag,2);tegu_pc_init(&pk,0,"build-A",kinds);assert(tegu_pc_attach_pack(&pk,pack));tegu_pc_close(&pk);
 WRITE_PACK(tag,3);tegu_pc_init(&pk,0,"build-A",kinds);assert(tegu_pc_attach_pack(&pk,pack));tegu_pc_close(&pk);
 WRITE_PACK(tag,0);assert(!truncate(pack,32+2*88+300+119));tegu_pc_init(&pk,0,"build-A",kinds);assert(tegu_pc_attach_pack(&pk,pack));tegu_pc_close(&pk);
 tegu_pc_init(&pk,0,"build-A",kinds);assert(tegu_pc_attach_pack(&pk,"/nonexistent/pack")==-2);tegu_pc_close(&pk);
 printf("PASS pipeline cache pack hit, corrupt payload, wrong build, bad index, out-of-range and truncated packs refused\n");

 /* Directory selection and permissions: a root-owned/unwritable leaf (the
  * 0700 root:mobile directory the probe left in mobile's Caches) is skipped
  * with its errno, an unset base is reported, duplicates are not retried, and
  * the first usable candidate wins.  Permission cases need a non-root user. */
 {
  char base[600];snprintf(base,sizeof(base),"%s/choose",dir);mkdir(base,0755);
  char locked[640],leafLocked[700],good[640],ro[640];
  snprintf(locked,sizeof(locked),"%s/locked",base);snprintf(good,sizeof(good),"%s/good",base);snprintf(ro,sizeof(ro),"%s/ro",base);
  mkdir(locked,0755);mkdir(good,0755);mkdir(ro,0755);
  snprintf(leafLocked,sizeof(leafLocked),"%s/com.tegu.metal.pipelines",locked);mkdir(leafLocked,0755);
  assert(!chmod(leafLocked,0500));  /* leaf exists, not writable: probe open fails */
  assert(!chmod(ro,0500));         /* no leaf, parent not writable: mkdir fails */
  struct tegu_pc_candidate cand[6]={{locked,""},{ro,""},{0,"/Library/Caches"},{"relative",""},{locked,""},{good,""}};
  char out[512],diag[1400];
  int idx=tegu_pc_choose_dir(cand,6,"com.tegu.metal.pipelines",out,sizeof(out),diag,sizeof(diag));
  if(geteuid()==0) {assert(idx==0);}
  else {
   assert(idx==5);
   char want[700];snprintf(want,sizeof(want),"%s/com.tegu.metal.pipelines",good);assert(!strcmp(out,want));
   assert(strstr(diag,"locked/com.tegu.metal.pipelines=mkdir:0,open:13,write:0"));  /* EACCES at the probe open */
   assert(strstr(diag,"ro/com.tegu.metal.pipelines=mkdir:13,open:2,write:0"));       /* EACCES at mkdir, ENOENT at open */
   assert(strstr(diag,"-=unset")&&strstr(diag,"relative=unset"));
   assert(strstr(diag,"good/com.tegu.metal.pipelines=ok"));
   size_t hits=0;for(const char *at=diag;(at=strstr(at,"locked/com.tegu"));at++)hits++;
   assert(hits==1); /* the duplicate candidate was not tried again */
  }
  assert(tegu_pc_choose_dir(cand,0,"x",out,sizeof(out),diag,sizeof(diag))==-1&&!out[0]);
  /* Entries are created 0644 even under a restrictive umask, so a process of
   * another uid can read them. */
  mode_t old=umask(077);
  char shared[700];snprintf(shared,sizeof(shared),"%s/shared",base);
  static struct tegu_pc sh;tegu_pc_init(&sh,shared,"build-A",kinds);
  tegu_pc_store(&sh,1,k1,payload,100);
  assert(!tegu_pc_slot_path(&sh,1,k1,path,sizeof(path)));
  struct stat st;assert(!stat(path,&st)&&(st.st_mode&0777)==0644);
  assert(!stat(shared,&st)&&(st.st_mode&0777)==0700); /* umask narrows the dir only */
  tegu_pc_append_record(&sh,"misses.txt","x\n",2,100);
  snprintf(path,sizeof(path),"%s/misses.txt",shared);assert(!stat(path,&st)&&(st.st_mode&0777)==0600);
  umask(old);
  tegu_pc_close(&sh);
  /* Disk traffic counters: reads attempted/absent, write failures with errno. */
  static struct tegu_pc cnt;tegu_pc_init(&cnt,shared,"build-A",kinds);
  unsigned char *pp=0;size_t nn=0;
  assert(tegu_pc_lookup(&cnt,1,k2,&pp,&nn)==0);
  struct tegu_pc_stats cs;tegu_pc_snapshot(&cnt,&cs);
  assert(cs.disk_reads==1&&cs.disk_read_absent==1&&cs.disk_writes==0);
  assert(tegu_pc_lookup(&cnt,1,k1,&pp,&nn)==2);free(pp);
  tegu_pc_snapshot(&cnt,&cs);assert(cs.disk_reads==2&&cs.disk_read_absent==1&&cs.disk_hits==1);
  tegu_pc_close(&cnt);
  static struct tegu_pc bad;tegu_pc_init(&bad,"/nonexistent-dir/x","build-A",kinds);
  for(unsigned i=0;i<5;i++)tegu_pc_store(&bad,1,k2,payload,50);
  tegu_pc_snapshot(&bad,&cs);
  assert(cs.disk_writes==0&&cs.disk_write_failures==4&&cs.last_write_errno==2); /* ENOENT, disabled after 4 */
  tegu_pc_close(&bad);
  printf("PASS pipeline cache directory selection with errno diagnostics, shared 0644 entries, disk read/write counters\n");
 }
 /* Miss records stay under their bound. */
 for(unsigned i=0;i<100;i++)tegu_pc_append_record(&next,"misses.txt","0123456789\n",11,500);
 snprintf(path,sizeof(path),"%s/misses.txt",dir);assert(file_size(path)==495);
 tegu_pc_close(&other);
 tegu_pc_close(&c);tegu_pc_close(&next);tegu_pc_close(&bad);tegu_pc_close(&rel);tegu_pc_close(&mem);
 return 0;
}
