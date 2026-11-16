/* In-boot A/B feature toggles (one device boot measures several variants).
 *
 * Schedule file: <bundle>/ABSchedule.conf (next to the Nord2Metal binary), or
 * the path in $TEGU_AB_SCHEDULE. Without a file every toggle keeps its usual
 * env/build default and nothing changes. Format, one directive per line,
 * '#' comments:
 *   baseline <seconds>          defaults only, from boot until <seconds>
 *   phase <seconds> [NAME=0|1]...  then phases in order; a phase without
 *                                  settings is a baseline phase
 *   repeat 0|1                  cycle through the phases (default 1)
 * NAME is one of NORD2_METAL_PREFRAME TEGU_NATIVE_INDEXED TEGU_ASYNC_PIPELINE
 * TEGU_TILE_FETCH TEGU_JM_BATCH (=0|1) and NORD2_METAL_BATCH_MAX (=32|128, the
 * bridge's draws-per-job cap, applied per command like the pre-frame mode). Time is seconds since boot
 * (mach_absolute_time), so every process follows the same schedule.
 *
 * Switching happens only at command-buffer boundaries: a command snapshots
 * the toggle word when it is created and uses it for its encode and its
 * execution (TeguCommand.abWord). Process-wide state is applied per command
 * at execution (the bridge's pre-frame mode); queue state (the pipelining
 * finish FIFO) keeps commit order across a switch. TEGU_TILE_FETCH (builds
 * with -DTEGU_TILE_FETCH_DEFAULT=0|1: tile fetch compiled in, off|on by
 * default) affects pipelines and specialized variants compiled while it is
 * active; variants are keyed by it, so both kinds coexist.
 *
 * A command that fails disables, for the rest of the boot, every toggle its
 * word had away from the baseline (TEGU_AB_DISABLED). Every TEGU_PERF window
 * carries the word and windows are cut when it changes. */
enum {TEGU_AB_PREFRAME,TEGU_AB_NATIVE_INDEXED,TEGU_AB_ASYNC_PIPELINE,TEGU_AB_TILE_FETCH,TEGU_AB_JM_BATCH,TEGU_AB_BATCH_MAX,TEGU_AB_TOGGLES};
static const char *const tegu_ab_names[TEGU_AB_TOGGLES]={"NORD2_METAL_PREFRAME","TEGU_NATIVE_INDEXED",
 "TEGU_ASYNC_PIPELINE","TEGU_TILE_FETCH","TEGU_JM_BATCH","NORD2_METAL_BATCH_MAX"};
static const char *const tegu_ab_short[TEGU_AB_TOGGLES]={"PF","NI","AP","TF","JB","BM"};
#define TEGU_AB_MAX_PHASES 32u
/* Word: bits 0..5 toggle values (BM: 1 = cap 128, 0 = 32), bit 7 "scheduled", bits 8..15 phase index
 * (0 = baseline before the first phase, k = phase k), bits 16..31 cycle. */
#define TEGU_AB_SCHEDULED 0x80u
struct tegu_ab_phase {unsigned seconds;uint8_t set,value;};
static struct {
 unsigned state; /* 0 unloaded, 1 loading, 2 ready */
 unsigned active,count,repeat,baseline;uint8_t defaults,uses,disabled;
 struct tegu_ab_phase phases[TEGU_AB_MAX_PHASES];unsigned total;
} tegu_ab;
#ifdef TEGU_FRONTEND_HOST
static double tegu_ab_test_now=-1; /* host tests: seconds since boot */
#endif
static double tegu_ab_boot_seconds(void) {
#ifdef TEGU_FRONTEND_HOST
 if(tegu_ab_test_now>=0)return tegu_ab_test_now;
#endif
 return tegu_host_seconds(); /* mach_absolute_time: since boot */
}
static int tegu_ab_env_bit(const char *name,int fallback) {
 extern char *getenv(const char *);
 const char *v=getenv(name);
 return v&&(v[0]=='0'||v[0]=='1')&&!v[1]?v[0]=='1':fallback;
}
/* The usual (unscheduled) value of every toggle, as the code read it before. */
static uint8_t tegu_ab_default_bits(void) {
 uint8_t bits=0;
#ifndef NORD2_PREFRAME_DEFAULT
#define TEGU_AB_PREFRAME_DEFAULT 0
#else
#define TEGU_AB_PREFRAME_DEFAULT NORD2_PREFRAME_DEFAULT
#endif
 if(tegu_ab_env_bit("NORD2_METAL_PREFRAME",TEGU_AB_PREFRAME_DEFAULT))bits|=1u<<TEGU_AB_PREFRAME;
 if(tegu_ab_env_bit("TEGU_NATIVE_INDEXED",1))bits|=1u<<TEGU_AB_NATIVE_INDEXED;
#ifndef TEGU_ASYNC_PIPELINE_DEFAULT
#define TEGU_ASYNC_PIPELINE_DEFAULT 0
#endif
 if(tegu_ab_env_bit("TEGU_ASYNC_PIPELINE",TEGU_ASYNC_PIPELINE_DEFAULT))bits|=1u<<TEGU_AB_ASYNC_PIPELINE;
#ifdef TEGU_TILE_FETCH_DEFAULT
 if(tegu_ab_env_bit("TEGU_TILE_FETCH",(TEGU_TILE_FETCH_DEFAULT)?1:0))bits|=1u<<TEGU_AB_TILE_FETCH;
#else
 if(tegu_ab_env_bit("TEGU_TILE_FETCH",0))bits|=1u<<TEGU_AB_TILE_FETCH;
#endif
#ifdef TEGU_JM_BATCH_DEFAULT
 if(tegu_ab_env_bit("TEGU_JM_BATCH",1))bits|=1u<<TEGU_AB_JM_BATCH;
#else
 if(tegu_ab_env_bit("TEGU_JM_BATCH",0))bits|=1u<<TEGU_AB_JM_BATCH;
#endif
 {extern char *getenv(const char *);const char *v=getenv("NORD2_METAL_BATCH_MAX");
  if(v&&v[0]=='1'&&v[1]=='2'&&v[2]=='8'&&!v[3])bits|=1u<<TEGU_AB_BATCH_MAX;}
 return bits;
}
static unsigned tegu_ab_number(const char **p) {
 unsigned v=0;while(**p==' '||**p=='\t')(*p)++;
 while(**p>='0'&&**p<='9'&&v<10000000u){v=v*10u+(unsigned)(**p-'0');(*p)++;}
 return v;
}
/* Parse the schedule text; returns 0 when it is valid (and non-empty). */
static int tegu_ab_parse(const char *text,size_t length) {
 unsigned count=0,baseline=0,repeat=1,total=0;uint8_t uses=0;struct tegu_ab_phase phases[TEGU_AB_MAX_PHASES];
 size_t at=0;
 while(at<length) {
  char line[256];size_t n=0;
  while(at<length&&text[at]!='\n'){if(n<sizeof(line)-1)line[n++]=text[at];at++;}
  at++;line[n]=0;
  for(size_t i=0;i<n;i++)if(line[i]=='#'){line[i]=0;break;}
  const char *p=line;while(*p==' '||*p=='\t'||*p=='\r')p++;
  if(!*p)continue;
  if(!__builtin_strncmp(p,"baseline",8)){p+=8;baseline=tegu_ab_number(&p);continue;}
  if(!__builtin_strncmp(p,"repeat",6)){p+=6;repeat=tegu_ab_number(&p)?1u:0u;continue;}
  if(__builtin_strncmp(p,"phase",5)||count>=TEGU_AB_MAX_PHASES)return -1;
  p+=5;struct tegu_ab_phase ph={tegu_ab_number(&p),0,0};
  if(!ph.seconds)return -1;
  for(;;) {
   while(*p==' '||*p=='\t'||*p=='\r')p++;
   if(!*p)break;
   unsigned t=0;
   for(;t<TEGU_AB_TOGGLES;t++){size_t k=__builtin_strlen(tegu_ab_names[t]);if(!__builtin_strncmp(p,tegu_ab_names[t],k)&&p[k]=='=')break;}
   if(t==TEGU_AB_TOGGLES)return -1;
   p+=__builtin_strlen(tegu_ab_names[t])+1;
   const char *digits=p;unsigned v=tegu_ab_number(&p);
   if(p==digits)return -1;
   if(t==TEGU_AB_BATCH_MAX){if(v!=32&&v!=128)return -1;v=v==128;}
   else if(v>1)return -1;
   if(*p&&*p!=' '&&*p!='\t'&&*p!='\r')return -1;
   ph.set|=(uint8_t)(1u<<t);if(v)ph.value|=(uint8_t)(1u<<t);uses|=(uint8_t)(1u<<t);
  }
  phases[count++]=ph;total+=ph.seconds;
 }
 if(!count)return -1;
 memcpy(tegu_ab.phases,phases,count*sizeof(phases[0]));
 tegu_ab.count=count;tegu_ab.baseline=baseline;tegu_ab.repeat=repeat;tegu_ab.total=total;tegu_ab.uses=uses;
 return 0;
}
static int tegu_ab_schedule_path(char *out,size_t size) {
 extern char *getenv(const char *);
 const char *env=getenv("TEGU_AB_SCHEDULE");
 if(env&&env[0]=='/'){int n=snprintf(out,size,"%s",env);return n>0&&(size_t)n<size;}
#if defined(__APPLE__) && defined(TEGU_GRAPHICS_BRIDGE) && !defined(TEGU_FRONTEND_HOST)
 struct tegu_dl_info self;
 if(tegu_dladdr((const void *)&tegu_bundle_base_marker,&self)&&self.fname) {
  size_t n=0,slash=0;while(self.fname[n]){if(self.fname[n]=='/')slash=n;n++;}
  if(slash&&slash+32<size){memcpy(out,self.fname,slash);snprintf(out+slash,size-slash,"/ABSchedule.conf");return 1;}
 }
#endif
 return 0;
}
static void tegu_ab_load(void) {
 unsigned expected=0;
 if(!__atomic_compare_exchange_n(&tegu_ab.state,&expected,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) {
  while(__atomic_load_n(&tegu_ab.state,__ATOMIC_ACQUIRE)!=2){}
  return;
 }
 tegu_ab.defaults=tegu_ab_default_bits();
 char path[512];
 if(tegu_ab_schedule_path(path,sizeof(path))) {
  extern int open(const char *,int,...);extern long read(int,void *,size_t);extern int close(int);
  int fd=open(path,0);
  if(fd>=0) {
   char text[8192];long n=read(fd,text,sizeof(text)-1);close(fd);
   char line[200];int k;
   if(n>0&&!tegu_ab_parse(text,(size_t)n)) {
    tegu_ab.active=1;
    k=snprintf(line,sizeof(line),"TEGU_AB schedule phases=%u baseline_s=%u cycle_s=%u repeat=%u defaults=0x%x uses=0x%x\n",
     tegu_ab.count,tegu_ab.baseline,tegu_ab.total,tegu_ab.repeat,tegu_ab.defaults,tegu_ab.uses);
   } else k=snprintf(line,sizeof(line),"TEGU_AB schedule refused (unreadable or invalid): %s\n",path);
   if(k>0)write(2,line,(size_t)k<sizeof(line)?(size_t)k:sizeof(line)-1);
  }
 }
#ifdef TEGU_AB_BUILTIN_DEFAULT
 /* Installed-addon updates cannot add files to the bundle, so a build can
  * carry tools/ab_schedule_default.conf in its binary; a schedule file wins. */
 if(!tegu_ab.active) {
  static const char builtin[]="baseline 60\nphase 45 TEGU_TILE_FETCH=1\nphase 45\nphase 45 NORD2_METAL_PREFRAME=1\nphase 45\n"
   "phase 45 NORD2_METAL_BATCH_MAX=128\nphase 45\nphase 45 TEGU_NATIVE_INDEXED=0\nphase 45\nphase 45 TEGU_ASYNC_PIPELINE=1\nphase 45\nrepeat 1\n";
  char line[200];int k;
  if(!tegu_ab_parse(builtin,sizeof(builtin)-1)) {
   tegu_ab.active=1;
   k=snprintf(line,sizeof(line),"TEGU_AB schedule builtin phases=%u baseline_s=%u cycle_s=%u repeat=%u defaults=0x%x uses=0x%x\n",
    tegu_ab.count,tegu_ab.baseline,tegu_ab.total,tegu_ab.repeat,tegu_ab.defaults,tegu_ab.uses);
  } else k=snprintf(line,sizeof(line),"TEGU_AB schedule builtin refused\n");
  if(k>0)write(2,line,(size_t)k<sizeof(line)?(size_t)k:sizeof(line)-1);
 }
#endif
 __atomic_store_n(&tegu_ab.state,2,__ATOMIC_RELEASE);
}
/* The toggle word in force now (a new command buffer's snapshot). */
static unsigned tegu_ab_current(void) {
 if(__atomic_load_n(&tegu_ab.state,__ATOMIC_ACQUIRE)!=2)tegu_ab_load();
 unsigned word=tegu_ab.defaults;
 if(!tegu_ab.active)return word;
 double now=tegu_ab_boot_seconds();
 if(now<(double)tegu_ab.baseline)return word|TEGU_AB_SCHEDULED;
 double into=now-(double)tegu_ab.baseline;unsigned cycle=0;
 if(tegu_ab.total) {
  unsigned whole=(unsigned)(into/(double)tegu_ab.total);
  if(whole&&!tegu_ab.repeat)return word|TEGU_AB_SCHEDULED|((tegu_ab.count+1u)<<8); /* finished: baseline */
  cycle=whole;into-=(double)whole*(double)tegu_ab.total;
 }
 unsigned k=0;while(k+1<tegu_ab.count&&into>=(double)tegu_ab.phases[k].seconds){into-=(double)tegu_ab.phases[k].seconds;k++;}
 uint8_t disabled=__atomic_load_n(&tegu_ab.disabled,__ATOMIC_RELAXED);
 uint8_t set=tegu_ab.phases[k].set&(uint8_t)~disabled;
 word=(word&~(unsigned)set)|(tegu_ab.phases[k].value&set);
 return word|TEGU_AB_SCHEDULED|((k+1u)<<8)|((cycle&0xffffu)<<16);
}
static int tegu_ab_value(unsigned word,unsigned toggle){return (word>>toggle)&1u;}
/* Whether a schedule may turn this toggle on in this process. */
static int tegu_ab_may_enable(unsigned toggle) {
 if(__atomic_load_n(&tegu_ab.state,__ATOMIC_ACQUIRE)!=2)tegu_ab_load();
 return ((tegu_ab.defaults|(tegu_ab.active?tegu_ab.uses:0))>>toggle)&1u;
}
static int tegu_ab_scheduled(unsigned word,unsigned toggle) {
 return (word&TEGU_AB_SCHEDULED)&&((tegu_ab.uses>>toggle)&1u);
}
/* "ab=3.1:PF1NI0AP0TF0JB1" (phase.cycle; "-" without a schedule). */
static int tegu_ab_tag(unsigned word,char *out,size_t size) {
 char phase[24];
 if(word&TEGU_AB_SCHEDULED)snprintf(phase,sizeof(phase),"%u.%u",(word>>8)&0xffu,word>>16);
 else snprintf(phase,sizeof(phase),"-");
 return snprintf(out,size,"ab=%s:%s%u%s%u%s%u%s%u%s%u%s%u",phase,tegu_ab_short[0],tegu_ab_value(word,0),tegu_ab_short[1],tegu_ab_value(word,1),
  tegu_ab_short[2],tegu_ab_value(word,2),tegu_ab_short[3],tegu_ab_value(word,3),tegu_ab_short[4],tegu_ab_value(word,4),
  tegu_ab_short[5],tegu_ab_value(word,5));
}
/* A command with this word failed: switch off, for the rest of the boot,
 * every scheduled toggle it had away from the baseline. */
static void tegu_ab_note_failure(unsigned word,int error) {
 if(!(word&TEGU_AB_SCHEDULED))return;
 uint8_t away=(uint8_t)((word^tegu_ab.defaults)&tegu_ab.uses&((1u<<TEGU_AB_TOGGLES)-1u));
 uint8_t fresh=away&(uint8_t)~__atomic_fetch_or(&tegu_ab.disabled,away,__ATOMIC_RELAXED);
 for(unsigned t=0;t<TEGU_AB_TOGGLES;t++)if(fresh&(1u<<t)) {
  char tag[64],line[200];tegu_ab_tag(word,tag,sizeof(tag));
  int k=snprintf(line,sizeof(line),"TEGU_AB_DISABLED toggle=%s value=%u error=%d %s\n",tegu_ab_names[t],tegu_ab_value(word,t),error,tag);
  if(k>0)write(2,line,(size_t)k<sizeof(line)?(size_t)k:sizeof(line)-1);
 }
}
/* The executing command's word (teguExecute), for execution-time readers. */
static __thread unsigned tegu_ab_exec_word;
static __thread unsigned tegu_ab_exec_set;
/* TEGU_TILE_FETCH (only with TEGU_TILE_FETCH_DEFAULT builds): read when a
 * pipeline or variant is compiled; the executing command's word if any. */
static __attribute__((unused)) int tegu_ab_tile_fetch(void) {
 return tegu_ab_value(tegu_ab_exec_set?tegu_ab_exec_word:tegu_ab_current(),TEGU_AB_TILE_FETCH);
}
