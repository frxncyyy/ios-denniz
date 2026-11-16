/* Host-only injected tests; included by test_compositor_diagnostics.py. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *test_log_path;
static int injected_errno, fork_result, wait_status, wait_result_mode;
static int wait_calls, wait_eintr_once, wait_saw_spawn_marker;
static int short_write_limit, write_eintr_once, runtime_calls;
static int fork_calls, control_calls, current_parent_limit=64;
static int memory_control_missing, memory_refuse_set, parent_budget_at_fork;
static int fake_fork(void);
static int fake_wait4(int, int *, int, void *);
static long injected_write(int, const void *, unsigned long);
static void *trap_dlopen(const char *, int);
static void *trap_dlsym(void *, const char *);
static int fake_getpid(void);
static int fake_memorystatus_control(unsigned,int,unsigned,void *,unsigned long);
#define TEGU_LOG_PATH test_log_path
#define TEGU_O_WRONLY O_WRONLY
#define TEGU_O_APPEND O_APPEND
#define TEGU_O_CREAT O_CREAT
#define TEGU_WRITE injected_write
#define TEGU_GET_ERRNO injected_errno
#define TEGU_FORK fake_fork
#define TEGU_WAIT4 fake_wait4
#define main tegu_actual_probe_main
#define dlopen trap_dlopen
#define dlsym trap_dlsym
#include "compositor_public_probe.c"
#undef dlsym
#undef dlopen
#undef main

static int fake_fork(void) { fork_calls++;parent_budget_at_fork=(current_parent_limit==512&&control_calls>=2);return fork_result; }
static int log_has_spawned(void) {
    FILE *f=fopen(test_log_path,"rb"); if(!f)return 0;
    char b[4096];size_t n=fread(b,1,sizeof(b)-1,f);fclose(f);b[n]=0;
    return strstr(b,"supervisor spawned pid=777")!=0;
}
static int fake_wait4(int pid,int *status,int options,void *usage) {
    (void)options;(void)usage;wait_calls++;
    wait_saw_spawn_marker=log_has_spawned();
    if(wait_eintr_once){wait_eintr_once=0;injected_errno=TEGU_EINTR;return -1;}
    if(wait_result_mode){injected_errno=wait_result_mode;return -1;}
    *status=wait_status;return pid;
}
static long injected_write(int fd,const void *data,unsigned long length) {
    if(write_eintr_once){write_eintr_once=0;injected_errno=TEGU_EINTR;return -1;}
    if(short_write_limit && length>(unsigned long)short_write_limit)length=(unsigned long)short_write_limit;
    return write(fd,data,length);
}
static void *trap_dlopen(const char *path,int flags){(void)flags;if(strstr(path,"libSystem"))return (void *)1;runtime_calls++;return 0;}
static void *trap_dlsym(void *handle,const char *name){
    (void)handle;
    if(!strcmp(name,"getpid"))return (void *)fake_getpid;
    if(!strcmp(name,"memorystatus_control"))return memory_control_missing?0:(void *)fake_memorystatus_control;
    runtime_calls++;return 0;
}
static int fake_getpid(void){return 4242;}
static int fake_memorystatus_control(unsigned command,int pid,unsigned flags,void *data,unsigned long size){
    (void)pid;(void)flags;(void)size;control_calls++;
    if(command==TEGU_MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES){
        struct memory_limits *m=(struct memory_limits *)data;m->active=current_parent_limit;m->inactive=current_parent_limit;return 0;
    }
    if(command==TEGU_MEMORYSTATUS_CMD_SET_MEMLIMIT_PROPERTIES){
        if(memory_refuse_set)return 1;
        struct memory_limits *m=(struct memory_limits *)data;current_parent_limit=m->active;return 0;
    }
    return 22;
}
static void reset_case(void){
    unlink(test_log_path);probe_log_failed=0;injected_errno=0;wait_calls=0;
    wait_eintr_once=0;wait_result_mode=0;wait_saw_spawn_marker=0;fork_calls=0;control_calls=0;
    current_parent_limit=64;memory_control_missing=0;memory_refuse_set=0;parent_budget_at_fork=0;
    short_write_limit=0;write_eintr_once=0;runtime_calls=0;fork_result=777;
}
static char *read_log(void){
    FILE *f=fopen(test_log_path,"rb");if(!f)return 0;
    char *b=calloc(1,65536);assert(b);size_t n=fread(b,1,65535,f);fclose(f);b[n]=0;return b;
}
static int run_parent(int status,const char *budget,int *rc,char **contents){
    char a0[]="probe",sup[]="--supervise",path[]="/local/QuartzCore.metallib";
    char *argv[5]={a0,sup,path,0,0};int argc=3;
    if(budget){argv[3]=(char *)budget;argc=4;}
    wait_status=status;
    *rc=tegu_actual_probe_main(argc,argv);*contents=read_log();return *contents!=0;
}
int compositor_supervisor_host_tests(const char *log_path){
    int failures=0,rc;char *log=0;test_log_path=log_path;
#define CHECK(c,label) do{if(!(c)){fprintf(stderr,"FAIL %s\n",label);failures++;}else fprintf(stderr,"PASS %s\n",label);}while(0)

    reset_case();wait_eintr_once=1;write_eintr_once=1;short_write_limit=7;
    CHECK(run_parent(0,0,&rc,&log)&&rc==0&&wait_calls==2&&wait_saw_spawn_marker&&
          strstr(log,"raw_status=0x0 exit=0 signal=0")&&!strstr(log,"PASS public Metal"),
          "success, spawn-before-wait, EINTR retry, short writes, no parent GPU PASS");free(log);

    reset_case();
    CHECK(run_parent(7<<8,0,&rc,&log)&&rc!=0&&strstr(log,"raw_status=0x700 exit=7 signal=0")&&
          strstr(log,"VERDICT FAIL")&&!strstr(log,"PASS public Metal"),"nonzero child exit fails");free(log);

    reset_case();
    CHECK(run_parent(9,0,&rc,&log)&&rc!=0&&strstr(log,"raw_status=0x9 exit=none signal=9")&&
          strstr(log,"VERDICT FAIL"),"signal death is decoded");free(log);

    reset_case();wait_result_mode=ECHILD;
    CHECK(run_parent(0,0,&rc,&log)&&rc!=0&&wait_calls==1&&strstr(log,"wait_failed pid=777 result=-1")&&
          strstr(log,"errno=10")&&strstr(log,"VERDICT FAIL"),"wait4 ECHILD failure fails durably");free(log);

    reset_case();
    CHECK(run_parent(0177,0,&rc,&log)&&rc!=0&&strstr(log,"state=unknown")&&strstr(log,"VERDICT FAIL"),
          "stopped/unknown wait state fails closed");free(log);

    reset_case();fork_result=-1;injected_errno=11;
    char f0[]="probe",fs[]="--supervise",fr[]="--replay";char *fv[]={f0,fs,fr,0};
    rc=tegu_actual_probe_main(3,fv);log=read_log();
    CHECK(rc!=0&&wait_calls==0&&strstr(log,"fork_failed errno=11")&&strstr(log,"VERDICT FAIL"),"fork failure fails without wait");free(log);

    reset_case();fork_result=0;
    FILE *seed=fopen(test_log_path,"wb");assert(seed);fputs("prior replay record\n",seed);fclose(seed);
    char c0[]="probe",cs[]="--supervise",cr[]="--replay";char *cv[]={c0,cs,cr,0};
    rc=tegu_actual_probe_main(3,cv);
    CHECK(rc==0&&runtime_calls==0&&wait_calls==0,"child preserves replay path and bypasses runtime init");
    reset_case();
    CHECK(run_parent(0,"512",&rc,&log)&&rc==0&&parent_budget_at_fork&&current_parent_limit==512&&control_calls==3&&
          strstr(log,"parent_budget role=supervisor_parent pid=4242 requested_mb=512 active")&&
          strstr(log,"supervisor spawned pid=777")&&
          strstr(log,"requested_mb=512 active")<strstr(log,"supervisor spawned pid=777"),
          "parent applies and reads back explicit cap before fork");free(log);

    reset_case();
    char i0[]="probe",is[]="--supervise",ip[]="/local/Q.metallib",ib[]="2048";char *iv[]={i0,is,ip,ib,0};
    rc=tegu_actual_probe_main(4,iv);log=read_log();
    CHECK(rc!=0&&fork_calls==0&&control_calls==0&&strstr(log,"invalid probe memory limit"),"invalid parent cap rejected before fork");free(log);

    reset_case();memory_control_missing=1;
    int got_log=run_parent(0,"512",&rc,&log);
    CHECK(got_log&&rc!=0&&fork_calls==0&&strstr(log,"memory_limit_api_missing")&&strstr(log,"requested_mb=512 refused"),
          "missing parent cap API refuses before fork");free(log);

    reset_case();memory_refuse_set=1;
    CHECK(run_parent(0,"512",&rc,&log)&&rc!=0&&fork_calls==0&&current_parent_limit==64,
          "refused parent cap prevents fork");free(log);
    unlink(test_log_path);
#undef CHECK
    return failures;
}
int main(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"usage: supervisor-host-test logfile\n");return 2;}
    return compositor_supervisor_host_tests(argv[1])?1:0;
}
