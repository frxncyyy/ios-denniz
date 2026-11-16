/* Public Objective-C runtime Metal probe for the supplied QuartzCore library.
 * SDK-free ABI declarations follow tools/tegu/metal/native_probe.c. */
#include "compositor_proof_facts.h"
#include "compositor_load_draw_plan.h"
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
#include "compositor_8tap_oracle.h"
#define TEGU_PUBLIC_FRAGMENT_NAME "downsample_8_frag_lph"
#if defined(TEGU_COMPOSITOR_LOAD_PROBE)
#error "8-tap expected pixels are not defined for LOAD mode"
#endif
#else
#define TEGU_PUBLIC_FRAGMENT_NAME "downsample_4_frag_lph"
#endif
#ifdef TEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY
#include "compositor_system_device_identity.h"
#endif
#ifdef TEGU_COMPOSITOR_LOAD_PROBE
#include <compositor_load_expected.h>
#endif
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
#include "compositor_public_iosurface_layout.h"
#endif
#ifdef TEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE
#include "../present/held_present.h"
#endif
#include <stdarg.h>
#include <stdint.h>
typedef unsigned long usize;
typedef unsigned long long u64;
typedef void *object;
typedef void *selector;
extern int vsnprintf(char *, usize, const char *, va_list);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
#ifdef TEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE
extern int usleep(unsigned int);
static int tegu_present_pause(uint32_t milliseconds,void *context) {
    (void)context;
    if(!milliseconds||milliseconds>1000u)return -1;
    return usleep(milliseconds*1000u);
}
static const char *tegu_present_status_name(enum tegu_present_status status) {
    switch(status) {
    case TEGU_PRESENT_PIXELS_MATCH:return "PIXELS_MATCH";
    case TEGU_PRESENT_INVALID_ARGUMENT:return "INVALID_ARGUMENT";
    case TEGU_PRESENT_TARGET_UNSUPPORTED:return "TARGET_UNSUPPORTED";
    case TEGU_PRESENT_IOKIT_ERROR:return "IOKIT_ERROR";
    case TEGU_PRESENT_PANEL_UNSUPPORTED:return "PANEL_UNSUPPORTED";
    case TEGU_PRESENT_ALREADY_MATCHED:return "ALREADY_MATCHED";
    case TEGU_PRESENT_READBACK_ERROR:return "READBACK_ERROR";
    case TEGU_PRESENT_TIMEOUT:return "TIMEOUT";
    case TEGU_PRESENT_API_UNAVAILABLE:return "API_UNAVAILABLE";
    default:return "UNKNOWN";
    }
}
#endif
extern char *dlerror(void);
extern int open(const char *, int, ...);
extern long read(int, void *, usize);
extern long write(int, const void *, usize);
extern long lseek(int, long, int);
extern int fsync(int);
extern int close(int);
extern int *__error(void);
extern void *malloc(usize);
extern void free(void *);
extern int fork(void);
extern int wait4(int, int *, int, void *);

static selector (*register_selector)(const char *);
static void *send_message;
#define SEL(s) register_selector(s)
#define MSG0(o, s) ((object (*)(object, selector))send_message)((o), SEL(s))
#define MSG1(o, s, a) ((object (*)(object, selector, object))send_message)((o), SEL(s), (a))
#define MSGU(o, s, a) ((void (*)(object, selector, usize))send_message)((o), SEL(s), (usize)(a))
#define MSGI(o, s, a) ((void (*)(object, selector, int))send_message)((o), SEL(s), (int)(a))
#define MSGF(o, s, a) ((void (*)(object, selector, float))send_message)((o), SEL(s), (float)(a))
/* Values are from the matched XNU bsd/sys/{fcntl.h,errno.h}: Darwin
 * O_WRONLY=1, O_APPEND=8, O_CREAT=0x200, EINTR=4. */
#ifndef TEGU_O_WRONLY
#define TEGU_O_WRONLY 0x0001
#endif
#ifndef TEGU_O_APPEND
#define TEGU_O_APPEND 0x0008
#endif
#ifndef TEGU_O_CREAT
#define TEGU_O_CREAT  0x0200
#endif
#ifndef TEGU_EINTR
#define TEGU_EINTR 4
#endif
#ifndef TEGU_LOG_PATH
#define TEGU_LOG_PATH "/var/log/tegu-compositor-public-probe.log"
#endif
#ifndef TEGU_COMPOSITOR_RTLD_NOW
#define TEGU_COMPOSITOR_RTLD_NOW 0x2
#endif
#ifndef TEGU_COMPOSITOR_RTLD_LOCAL
#define TEGU_COMPOSITOR_RTLD_LOCAL 0x4
#endif
#ifndef TEGU_WRITE
#define TEGU_WRITE write
#endif
#ifndef TEGU_OPEN
#define TEGU_OPEN open
#endif
#ifndef TEGU_FSYNC
#define TEGU_FSYNC fsync
#endif
#ifndef TEGU_CLOSE
#define TEGU_CLOSE close
#endif
#ifndef TEGU_GET_ERRNO
#define TEGU_GET_ERRNO (*__error())
#endif
#ifndef TEGU_FORK
#define TEGU_FORK fork
#endif
#ifndef TEGU_WAIT4
#define TEGU_WAIT4 wait4
#endif
#define TEGU_LOG_RECORD_MAX 512
static int probe_log_failed;
static int probe_console_failed;
enum durable_stage { DURABLE_STAGE_NONE, DURABLE_STAGE_OPEN, DURABLE_STAGE_WRITE,
    DURABLE_STAGE_FSYNC, DURABLE_STAGE_CLOSE, DURABLE_STAGE_FORMAT };
static enum durable_stage first_durable_stage;
static int first_durable_errno;
static const char *durable_stage_name(enum durable_stage stage) {
    switch(stage) {
    case DURABLE_STAGE_OPEN:return "open";
    case DURABLE_STAGE_WRITE:return "write";
    case DURABLE_STAGE_FSYNC:return "fsync";
    case DURABLE_STAGE_CLOSE:return "close";
    case DURABLE_STAGE_FORMAT:return "format";
    default:return "none";
    }
}
static void note_durable_failure(enum durable_stage stage,int error) {
    if(first_durable_stage==DURABLE_STAGE_NONE) {
        first_durable_stage=stage;
        first_durable_errno=error;
    }
    probe_log_failed=1;
}
static int format_record(char *out,usize capacity,const char *format,...) {
    va_list ap;va_start(ap,format);
    int n=vsnprintf(out,capacity,format,ap);
    va_end(ap);return n;
}
static int write_all(int fd,const void *bytes,usize length) {
    const unsigned char *p=(const unsigned char *)bytes;
    while(length) {
        long n=TEGU_WRITE(fd,p,length);
        if(n>0) { p+=(usize)n;length-=(usize)n;continue; }
        if(n<0 && TEGU_GET_ERRNO==TEGU_EINTR) continue;
        return -1;
    }
    return 0;
}
static int append_durable(const char *line,usize length) {
    int fd=TEGU_OPEN(TEGU_LOG_PATH,TEGU_O_WRONLY|TEGU_O_CREAT|TEGU_O_APPEND,0600);
    if(fd<0) {int error=TEGU_GET_ERRNO;note_durable_failure(DURABLE_STAGE_OPEN,error);return -1;}
    int bad=0;
    if(write_all(fd,line,length)) {
        int error=TEGU_GET_ERRNO;note_durable_failure(DURABLE_STAGE_WRITE,error);bad=-1;
    }
    if(!bad&&TEGU_FSYNC(fd)) {
        int error=TEGU_GET_ERRNO;note_durable_failure(DURABLE_STAGE_FSYNC,error);bad=-1;
    }
    if(TEGU_CLOSE(fd)) {
        int error=TEGU_GET_ERRNO;note_durable_failure(DURABLE_STAGE_CLOSE,error);bad=-1;
    }
    return bad;
}
static int append_failure_verdict(void) {
    char record[160];
    int n=format_record(record,sizeof(record),
        "TEGU_COMPOSITOR VERDICT FAIL durable_log stage=%s errno=%d\n",
        durable_stage_name(first_durable_stage),first_durable_errno);
    if(n<0||(usize)n>=sizeof(record))return -1;
    /* This is intentionally attempted despite the sticky failure flag. Never
     * clear that flag: only the caller's nonzero exit reports a failed run. */
    return append_durable(record,(usize)n);
}
static int emit_log_record(const char *line,usize length) {
    /* A visible phase marker must mean its file flush already returned.
     * Printing first would make a blocked fsync look like a blocked next call. */
    if(append_durable(line,length)) {
        static const char failure[]="TEGU_COMPOSITOR FAILED durable_log\n";
        probe_log_failed=1;
        if(!probe_console_failed&&write_all(1,failure,sizeof(failure)-1)) {
            probe_console_failed=1;
        }
        return -1;
    }
    if(!probe_console_failed&&write_all(1,line,length)) {
        char diagnostic[TEGU_LOG_RECORD_MAX];
        int saved_errno=TEGU_GET_ERRNO;
        probe_console_failed=1;
        usize context_bytes=length;
        if(context_bytes&&line[context_bytes-1]=='\n')context_bytes--;
        if(context_bytes>180)context_bytes=180;
        int n=format_record(diagnostic,sizeof(diagnostic),
            "TEGU_COMPOSITOR console_output_disabled errno=%d after=%.*s\n",
            saved_errno,(int)context_bytes,line);
        if(n<0||(usize)n>=sizeof(diagnostic)||append_durable(diagnostic,(usize)n)) {
            probe_log_failed=1;
            return -1;
        }
    }
    return 0;
}
static int probe_log(const char *format,...) {
    char record[TEGU_LOG_RECORD_MAX];
    va_list ap;va_start(ap,format);
    int n=vsnprintf(record,sizeof(record),format,ap);
    va_end(ap);
    if(n<0||(usize)n>=sizeof(record)) {
        static const char failure[]="TEGU_COMPOSITOR log_record_truncated\n";
        note_durable_failure(DURABLE_STAGE_FORMAT,0);
        if(!probe_console_failed&&write_all(1,failure,sizeof(failure)-1)) {
            probe_console_failed=1;
        }
        (void)append_durable(failure,sizeof(failure)-1);
        return -1;
    }
    return emit_log_record(record,(usize)n);
}
#define LOG(...) ((void)probe_log(__VA_ARGS__))

static int replay_log(void) {
    int fd=open(TEGU_LOG_PATH,0);
    if(fd<0)return 1;
    char bytes[1024];usize total=0;int failed=0;
    for(;;) {
        long n=read(fd,bytes,sizeof(bytes));
        if(n==0)break;
        if(n<0) {if(TEGU_GET_ERRNO==TEGU_EINTR)continue;failed=1;break;}
        total+=(usize)n;if(total>(1u<<20)){failed=1;break;}
        if(write_all(1,bytes,(usize)n)){failed=1;break;}
    }
    if(close(fd))failed=1;
    return failed?1:0;
}
static int is_replay_arg(const char *s) {
    static const char expected[]="--replay";
    if(!s)return 0;
    usize i=0;while(expected[i]&&s[i]==expected[i])i++;
    return expected[i]==0&&s[i]==0;
}
static int is_supervise_arg(const char *s) {
    static const char expected[]="--supervise";
    if(!s)return 0;
    usize i=0;while(expected[i]&&s[i]==expected[i])i++;
    return expected[i]==0&&s[i]==0;
}
static int parse_probe_memory(const char *s, unsigned *value) {
    unsigned parsed=0,digits=0;
    if(!s||!value)return -1;
    while(*s&&digits<4&&*s>='0'&&*s<='9'){parsed=parsed*10u+(unsigned)(*s++-'0');digits++;}
    if(*s||!digits||parsed<128||parsed>1024)return -1;
    *value=parsed;return 0;
}
int main(int argc, char **argv);
static int configure_probe_memory(object system,unsigned requested);
static int terminal_verdict(int rc) {
    static const char pass[]="TEGU_COMPOSITOR PASS public Metal pair draw and CPU-readback guarded output\n";
    static const char fail[]="TEGU_COMPOSITOR VERDICT FAIL\n";
    if(rc||probe_log_failed) {
        if(!probe_log_failed) {
            if(emit_log_record(fail,sizeof(fail)-1)&&probe_log_failed)
                (void)append_failure_verdict();
        } else {
            (void)append_failure_verdict();
            if(!probe_console_failed&&write_all(1,fail,sizeof(fail)-1))
                probe_console_failed=1;
        }
        return rc?rc:1;
    }
    if(emit_log_record(pass,sizeof(pass)-1)) {
        if(probe_log_failed)(void)append_failure_verdict();
        return 1;
    }
    return 0;
}

/* XNU bsd/sys/wait.h status encoding; child exit/signal is not a GPU verdict. */
#define TEGU_WSTATUS(s) ((s) & 0177)
#define TEGU_WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define TEGU_WIFEXITED(s) (TEGU_WSTATUS(s) == 0)
#define TEGU_WIFSIGNALED(s) (TEGU_WSTATUS(s) != 0177 && TEGU_WSTATUS(s) != 0)
#define TEGU_WTERMSIG(s) TEGU_WSTATUS(s)
static int supervise_probe(int argc, char **argv) {
    unsigned requested_memory=0;
    int replay=(argc==3&&is_replay_arg(argv[2]));
    int load_mode=!replay&&argc>=5&&argv[3]&&argv[3][0]=='-'&&argv[3][1]=='-'&&__builtin_strcmp(argv[3],"--compositor-load-two-draws")==0;
    int surface_mode=!replay&&argc>=4&&argv[3]&&__builtin_strcmp(argv[3],"--iosurface-target")==0;
    int present_mode=surface_mode&&argc>=5&&argv[4]&&__builtin_strcmp(argv[4],"--present-iosurface")==0;
    if(!replay&&!(argc==3||(argc==4&&!surface_mode)||(surface_mode&&((argc==4&&!present_mode)||(argc==5)||(argc==6&&present_mode)))||(argc==5&&load_mode)||(argc==6&&load_mode))){
        LOG("TEGU_COMPOSITOR supervisor usage requires metallib and optional compositor mode/alpha/limit\n");
        return terminal_verdict(2);
    }
    int has_memory=!replay&&((argc==4&&!surface_mode)||(surface_mode&&((argc==5&&!present_mode)||(argc==6&&present_mode)))||(argc==6&&load_mode));
    if(has_memory&&parse_probe_memory(argv[argc-1],&requested_memory)){
        LOG("TEGU_COMPOSITOR supervisor invalid probe memory limit\n");
        return terminal_verdict(2);
    }
    if(requested_memory){
        object system=dlopen("/usr/lib/libSystem.B.dylib",2);
        int (*parent_getpid)(void)=system?(int (*)(void))dlsym(system,"getpid"):0;
        if(!system||!parent_getpid){
            LOG("TEGU_COMPOSITOR supervisor parent_budget api_missing\n");
            return terminal_verdict(1);
        }
        int pid=parent_getpid();
        LOG("TEGU_COMPOSITOR supervisor parent_budget role=supervisor_parent pid=%d requested_mb=%u begin\n",pid,requested_memory);
        if(probe_log_failed)return terminal_verdict(1);
        if(configure_probe_memory(system,requested_memory)){
            LOG("TEGU_COMPOSITOR supervisor parent_budget role=supervisor_parent pid=%d requested_mb=%u refused\n",pid,requested_memory);
            return terminal_verdict(1);
        }
        LOG("TEGU_COMPOSITOR supervisor parent_budget role=supervisor_parent pid=%d requested_mb=%u active\n",pid,requested_memory);
        if(probe_log_failed)return terminal_verdict(1);
    }
    int child = TEGU_FORK();
    if (child < 0) {
        LOG("TEGU_COMPOSITOR supervisor fork_failed errno=%d\n", TEGU_GET_ERRNO);
        return terminal_verdict(1);
    }
    if (child == 0) {
        for (int i = 1; i < argc - 1; ++i) argv[i] = argv[i + 1];
        argv[argc - 1] = 0;
        return main(argc - 1, argv);
    }
    LOG("TEGU_COMPOSITOR supervisor spawned pid=%d\n", child);
    int status = 0, waited;
    do {
        waited = TEGU_WAIT4(child, &status, 0, 0);
    } while (waited < 0 && TEGU_GET_ERRNO == TEGU_EINTR);
    if (waited != child) {
        LOG("TEGU_COMPOSITOR supervisor wait_failed pid=%d result=%d errno=%d\n", child, waited, TEGU_GET_ERRNO);
        return terminal_verdict(1);
    }
    if (TEGU_WIFEXITED(status)) {
        int code = TEGU_WEXITSTATUS(status);
        LOG("TEGU_COMPOSITOR supervisor child pid=%d raw_status=0x%x exit=%d signal=0\n", child, status, code);
        if (probe_log_failed) return terminal_verdict(1);
        return code ? terminal_verdict(1) : 0;
    }
    if (TEGU_WIFSIGNALED(status)) {
        LOG("TEGU_COMPOSITOR supervisor child pid=%d raw_status=0x%x exit=none signal=%d\n", child, status, TEGU_WTERMSIG(status));
    } else {
        LOG("TEGU_COMPOSITOR supervisor child pid=%d raw_status=0x%x exit=none signal=none state=unknown\n", child, status);
    }
    return terminal_verdict(1);
}

struct region_origin { usize x, y, z; };
struct region_size { usize width, height, depth; };
struct region { struct region_origin origin; struct region_size size; };
struct clear_color { double red, green, blue, alpha; };
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
struct tegu_public_vertex48 {
    float position[4];
    float texcoord0[2];
    float texcoord1[2];
    unsigned short color_half[4];
    unsigned char reserved[8];
};
_Static_assert(sizeof(struct tegu_public_vertex48)==48,"captured UI vertex stride");
#endif
/* XNU kern_memorystatus.h commands7/8; a diagnostic override affects only
 * this process. Fatal limits remain enabled and no system limit is changed. */
/* Exact ABI from the matched XNU bsd/sys/kern_memorystatus.h. */
struct memory_limits { int32_t active; uint32_t active_attributes; int32_t inactive; uint32_t inactive_attributes; };
_Static_assert(sizeof(struct memory_limits)==16,"XNU memorystatus memlimit ABI");
#define TEGU_MEMORYSTATUS_CMD_SET_MEMLIMIT_PROPERTIES 7u
#define TEGU_MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES 8u
#define TEGU_MEMORYSTATUS_MEMLIMIT_ATTR_FATAL 0x1u
static int configure_probe_memory(object system,unsigned requested) {
    int (*control)(unsigned,int,unsigned,void *,usize) =
        (int (*)(unsigned,int,unsigned,void *,usize))dlsym(system,"memorystatus_control");
    int (*get_pid)(void) = (int (*)(void))dlsym(system,"getpid");
    if (!control || !get_pid) { LOG("TEGU_COMPOSITOR memory_limit_api_missing\n"); return requested ? -1 : 0; }
    struct memory_limits before = {0}; int pid = get_pid();
    int rc = control(TEGU_MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES,pid,0,&before,sizeof(before));
    LOG("TEGU_COMPOSITOR memory_limits_before rc=%d active_mb=%d inactive_mb=%d\n",rc,before.active,before.inactive);
    if (!requested) return 0;
    if (rc) return -1;
    struct memory_limits desired = {(int32_t)requested,TEGU_MEMORYSTATUS_MEMLIMIT_ATTR_FATAL,
        (int32_t)requested,TEGU_MEMORYSTATUS_MEMLIMIT_ATTR_FATAL}, after = {0};
    rc = control(TEGU_MEMORYSTATUS_CMD_SET_MEMLIMIT_PROPERTIES,pid,0,&desired,sizeof(desired));
    if (!rc) rc = control(TEGU_MEMORYSTATUS_CMD_GET_MEMLIMIT_PROPERTIES,pid,0,&after,sizeof(after));
    LOG("TEGU_COMPOSITOR memory_limits_requested mb=%u rc=%d active_mb=%d inactive_mb=%d\n",requested,rc,after.active,after.inactive);
    return rc || after.active!=(int)requested || after.inactive!=(int)requested ? -1 : 0;
}

static void safe_release(object o) {
    if (o) ((void (*)(object, selector))send_message)(o, SEL("release"));
}
#ifdef TEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY
static uint64_t tegu_system_registry_id(void *o) {
    return (uint64_t)((uint64_t (*)(object, selector))send_message)
        ((object)o, SEL("registryID"));
}
static void tegu_system_release(void *o) { safe_release((object)o); }
#endif
static object retained(object o) {
    return o ? MSG0(o, "retain") : 0;
}
static void log_error(const char *phase, object error) {
    const char *description = "(none)";
    if (error) {
        object value = MSG0(error, "localizedDescription");
        const char *text = value ? (const char *)MSG0(value, "UTF8String") : 0;
        if (text) description = text;
    }
    LOG("TEGU_COMPOSITOR %s error=%s\n", phase, description);
}
#ifdef TEGU_COMPOSITOR_DIRECT_BUNDLE
/* Diagnostic alternative to MTLCreateSystemDefaultDevice. This checks direct
 * bundle loading and class identity; it does not claim system discovery. */
#define TEGU_COMPOSITOR_BUNDLE_PATH "/System/Library/Extensions/TeguMetal.bundle"
#ifdef TEGU_COMPOSITOR_DIRECT_DLOPEN_PREFLIGHT
/* Kept for the process lifetime so NSBundle sees the same image already
 * loaded by the explicit dyld preflight. This mode is diagnostic only. */
static object tegu_direct_bundle_preflight_handle;
#endif
static object tegu_create_direct_bundle_device(object foundation,
 object (*get_class)(const char *),object (*get_superclass)(object)) {
    if(probe_log_failed||!foundation||!get_class||!get_superclass)return 0;
    LOG("TEGU_COMPOSITOR phase direct_bundle runtime_classes begin\n");
    if(probe_log_failed)return 0;
    object string_class=get_class("NSString"),bundle_class=get_class("NSBundle");
    if(!string_class||!bundle_class) {
        LOG("TEGU_COMPOSITOR direct_bundle runtime_classes_missing\n");return 0;
    }
    LOG("TEGU_COMPOSITOR phase direct_bundle path_string begin\n");
    if(probe_log_failed)return 0;
    object path=((object (*)(object,selector,const char *))send_message)
        (string_class,SEL("stringWithUTF8String:"),TEGU_COMPOSITOR_BUNDLE_PATH);
    LOG("TEGU_COMPOSITOR phase direct_bundle bundle_object begin path_present=%d\n",path!=0);
    if(probe_log_failed)return 0;
    object bundle=path?MSG1(bundle_class,"bundleWithPath:",path):0;
    if(!bundle) {LOG("TEGU_COMPOSITOR direct_bundle bundle_object_missing\n");return 0;}
    object executable_path=0;
    const char *executable_utf8=0;
#ifdef TEGU_COMPOSITOR_DIRECT_DLOPEN_PREFLIGHT
    LOG("TEGU_COMPOSITOR phase direct_bundle executable_path begin\n");
    if(probe_log_failed)return 0;
    executable_path=MSG0(bundle,"executablePath");
    executable_utf8=executable_path?
        (const char *)MSG0(executable_path,"UTF8String"):0;
    if(!executable_utf8) {
        LOG("TEGU_COMPOSITOR direct_bundle dlopen_preflight path_missing\n");
        return 0;
    }
    LOG("TEGU_COMPOSITOR phase direct_bundle dlopen_preflight begin path=%s\n",
        executable_utf8);
    if(probe_log_failed)return 0;
    (void)dlerror(); /* Clear stale loader state before this single operation. */
    object preflight=dlopen(executable_utf8,
        TEGU_COMPOSITOR_RTLD_NOW|TEGU_COMPOSITOR_RTLD_LOCAL);
    if(!preflight) {
        char *load_error=dlerror();
        LOG("TEGU_COMPOSITOR direct_bundle dlopen_preflight returned present=0 error=%s\n",
            load_error?load_error:"(unavailable)");
        return 0;
    }
    tegu_direct_bundle_preflight_handle=preflight;
    LOG("TEGU_COMPOSITOR direct_bundle dlopen_preflight returned present=1\n");
    if(probe_log_failed)return 0;
#endif
    object error=0;
    LOG("TEGU_COMPOSITOR phase direct_bundle load begin\n");
    if(probe_log_failed)return 0;
    signed char loaded=((signed char (*)(object,selector,object *))send_message)
        (bundle,SEL("loadAndReturnError:"),&error);
    LOG("TEGU_COMPOSITOR direct_bundle load_result=%d\n",loaded!=0);
#ifdef TEGU_COMPOSITOR_DIRECT_DLOPEN_PREFLIGHT
    LOG("TEGU_COMPOSITOR direct_bundle dlopen_handle_retained=%d\n",
        tegu_direct_bundle_preflight_handle!=0);
#endif
    if(probe_log_failed)return 0;
    if(error) {log_error("direct_bundle_load",error);if(probe_log_failed)return 0;}
    if(!loaded)return 0;
    object bundle_path=MSG0(bundle,"bundlePath");
    executable_path=MSG0(bundle,"executablePath");
    executable_utf8=executable_path?(const char *)MSG0(executable_path,"UTF8String"):0;
    const char *bundle_utf8=bundle_path?(const char *)MSG0(bundle_path,"UTF8String"):0;
    LOG("TEGU_COMPOSITOR direct_bundle image bundle_path=%s executable_path=%s\n",
        bundle_utf8?bundle_utf8:"(unavailable)",executable_utf8?executable_utf8:"(unavailable)");
    if(probe_log_failed)return 0;
    LOG("TEGU_COMPOSITOR phase direct_bundle principal_class begin\n");
    if(probe_log_failed)return 0;
    object cls=MSG0(bundle,"principalClass");
    object expected=get_class("TeguComputeDevice"),metal_device=get_class("_MTLDevice");
    int class_identity=cls&&expected&&cls==expected;
    int superclass_identity=class_identity&&metal_device&&get_superclass(cls)==metal_device;
    LOG("TEGU_COMPOSITOR direct_bundle class_identity=%d superclass_mtl_device=%d\n",
        class_identity,superclass_identity);
    if(probe_log_failed||!class_identity||!superclass_identity)return 0;
    LOG("TEGU_COMPOSITOR phase direct_bundle_device alloc_init begin\n");
    if(probe_log_failed)return 0;
    object allocated=MSG0(cls,"alloc");
    object device=allocated?MSG0(allocated,"init"):0;
    LOG("TEGU_COMPOSITOR phase direct_bundle_device alloc_init returned present=%d\n",device!=0);
    return device;
}
#endif
static int read_library_file(const char *path, void **bytes, usize *length) {
    if (!path || !bytes || !length) return -1;
    int fd = open(path, 0);
    if (fd < 0) return -2;
    long end = lseek(fd, 0, 2);
    if (end <= 0 || end > (64L << 20) || lseek(fd, 0, 0) < 0) { close(fd); return -3; }
    void *data = malloc((usize)end);
    if (!data) { close(fd); return -4; }
    usize done = 0;
    while (done < (usize)end) {
        long n = read(fd, (unsigned char *)data + done, (usize)end - done);
        if (n <= 0) { free(data); close(fd); return -5; }
        done += (usize)n;
    }
    close(fd); *bytes = data; *length = (usize)end; return 0;
}
static object make_texture_descriptor(object cls, object send, object (*sel)(const char *),
                                      unsigned format, unsigned usage) {
    object d = ((object (*)(object, selector))send)(cls, sel("new"));
    if (!d) return 0;
    ((void (*)(object, selector, usize))send)(d, sel("setTextureType:"), 2);
    ((void (*)(object, selector, usize))send)(d, sel("setPixelFormat:"), format);
    ((void (*)(object, selector, usize))send)(d, sel("setWidth:"), 16);
    ((void (*)(object, selector, usize))send)(d, sel("setHeight:"), 16);
    ((void (*)(object, selector, usize))send)(d, sel("setDepth:"), 1);
    ((void (*)(object, selector, usize))send)(d, sel("setMipmapLevelCount:"), 1);
    ((void (*)(object, selector, usize))send)(d, sel("setArrayLength:"), 1);
    ((void (*)(object, selector, usize))send)(d, sel("setSampleCount:"), 1);
    ((void (*)(object, selector, usize))send)(d, sel("setResourceOptions:"), 0);
    ((void (*)(object, selector, usize))send)(d, sel("setUsage:"), usage);
    (void)cls;
    return d;
}

#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
struct tegu_public_iosurface_api {
    object (*create)(object);void (*release)(object);int (*lock)(object,unsigned,unsigned *);
    int (*unlock)(object,unsigned,unsigned *);void *(*base)(object);
    usize (*width)(object),(*height)(object),(*row_bytes)(object),(*alloc_bytes)(object),(*plane_count)(object);
    unsigned (*pixel_format)(object);uint32_t (*surface_id)(object);
};
static object tegu_iosurface_number(object cls,const char *key,unsigned long long value,
    object (*get_class)(const char *)) {
    object k=((object (*)(object,selector,const char *))send_message)(get_class("NSString"),SEL("stringWithUTF8String:"),key);
    object v=((object (*)(object,selector,unsigned long long))send_message)(get_class("NSNumber"),SEL("numberWithUnsignedLongLong:"),value);
    if(!k||!v)return 0;
    ((void (*)(object,selector,object,object))send_message)(cls,SEL("setObject:forKey:"),v,k);return cls;
}
static int tegu_iosurface_target_create(object get_class(const char *),object device,object descriptor,
    object *surface_out,object *texture_out,uint32_t *surface_id_out,
    struct tegu_public_iosurface_api *api) {
    void *ios=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",2);
    void *cf=dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",2);
    if(!ios||!cf)return -1;
    api->create=(object (*)(object))dlsym(ios,"IOSurfaceCreate");
    api->release=(void (*)(object))dlsym(cf,"CFRelease");
    api->lock=(int (*)(object,unsigned,unsigned *))dlsym(ios,"IOSurfaceLock");
    api->unlock=(int (*)(object,unsigned,unsigned *))dlsym(ios,"IOSurfaceUnlock");
    api->base=(void *(*)(object))dlsym(ios,"IOSurfaceGetBaseAddress");
    api->width=(usize (*)(object))dlsym(ios,"IOSurfaceGetWidth");
    api->height=(usize (*)(object))dlsym(ios,"IOSurfaceGetHeight");
    api->row_bytes=(usize (*)(object))dlsym(ios,"IOSurfaceGetBytesPerRow");
    api->alloc_bytes=(usize (*)(object))dlsym(ios,"IOSurfaceGetAllocSize");
    api->plane_count=(usize (*)(object))dlsym(ios,"IOSurfaceGetPlaneCount");
    api->pixel_format=(unsigned (*)(object))dlsym(ios,"IOSurfaceGetPixelFormat");
    api->surface_id=(uint32_t (*)(object))dlsym(ios,"IOSurfaceGetID");
    if(!api->create||!api->release||!api->lock||!api->unlock||!api->base||!api->width||!api->height||
       !api->row_bytes||!api->alloc_bytes||!api->plane_count||!api->pixel_format||!api->surface_id)return -2;
    object dictionary=((object (*)(object,selector))send_message)(get_class("NSMutableDictionary"),SEL("dictionary"));
    if(!dictionary)return -3;
    const char *keys[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfaceBytesPerRow",
        "IOSurfaceAllocSize","IOSurfacePixelFormat","IOSurfaceOffset",
#ifndef TEGU_COMPOSITOR_IOSURFACE_ORDINARY_PROBE
        "IOSurfaceTeguGPUShared"
#endif
    };
    const unsigned long long vals[]={TEGU_PUBLIC_SURFACE_WIDTH,TEGU_PUBLIC_SURFACE_HEIGHT,4,
        TEGU_PUBLIC_SURFACE_ROW_BYTES,TEGU_PUBLIC_SURFACE_REQUEST_ALLOC_BYTES,0x42475241u,
        TEGU_PUBLIC_SURFACE_OFFSET,1};
    for(unsigned i=0;i<7;i++)if(!tegu_iosurface_number(dictionary,keys[i],vals[i],get_class))return -4;
#ifndef TEGU_COMPOSITOR_IOSURFACE_ORDINARY_PROBE
    object sharedKey=((object (*)(object,selector,const char *))send_message)(get_class("NSString"),SEL("stringWithUTF8String:"),keys[7]);
    object sharedValue=((object (*)(object,selector,int))send_message)(get_class("NSNumber"),SEL("numberWithBool:"),1);
    if(!sharedKey||!sharedValue)return -5;
    ((void (*)(object,selector,object,object))send_message)(dictionary,SEL("setObject:forKey:"),sharedValue,sharedKey);
#endif
    object surface=api->create(dictionary);
    if(!surface)return -6;
    /* Offset is deliberately zero: don't guess whether Apple's base getter
     * already incorporates a nonzero IOSurfaceOffset. The actual getter is
     * the bound for all CPU accesses below; request 16 KiB so kernel page
     * rounding cannot make the imported backing smaller than our guard range. */
    usize actual_alloc=api->alloc_bytes(surface);
    struct tegu_public_surface_layout layout={TEGU_PUBLIC_SURFACE_WIDTH,TEGU_PUBLIC_SURFACE_HEIGHT,
        (uint32_t)api->row_bytes(surface),TEGU_PUBLIC_SURFACE_OFFSET,(uint32_t)actual_alloc};
    if(api->surface_id(surface)==0||api->width(surface)!=TEGU_PUBLIC_SURFACE_WIDTH||
       api->height(surface)!=TEGU_PUBLIC_SURFACE_HEIGHT||api->row_bytes(surface)!=TEGU_PUBLIC_SURFACE_ROW_BYTES||
       actual_alloc!=TEGU_PUBLIC_SURFACE_REQUEST_ALLOC_BYTES||actual_alloc>UINT32_MAX||api->pixel_format(surface)!=0x42475241u||api->plane_count(surface)!=0||
       !tegu_public_surface_layout_valid(&layout)) {api->release(surface);return -7;}
    unsigned seed=0;if(api->lock(surface,0,&seed)){api->release(surface);return -8;}
    unsigned char *base=api->base(surface);if(!base){api->unlock(surface,0,&seed);api->release(surface);return -9;}
    for(usize i=0;i<actual_alloc;i++)base[i]=0xa5;
    if(api->unlock(surface,0,&seed)){api->release(surface);return -10;}
    selector make=SEL("newTextureWithDescriptor:iosurface:plane:");
    signed char (*responds)(object,selector,selector)=(void *)send_message;
    if(!responds(device,SEL("respondsToSelector:"),make)){api->release(surface);return -11;}
    object texture=((object (*)(object,selector,object,object,usize))send_message)(device,make,descriptor,surface,0);
    if(!texture){api->release(surface);return -12;}
    object actual=((object (*)(object,selector))send_message)(texture,SEL("iosurface"));
    if(actual!=surface){((void (*)(object,selector))send_message)(texture,SEL("release"));api->release(surface);return -13;}
    *surface_out=surface;*texture_out=texture;*surface_id_out=api->surface_id(surface);return 0;
}
#endif

int main(int argc, char **argv) {
    unsigned requested_memory = 0;
    unsigned source_alpha=0,source_alpha_second=0;
    int load_two_draws=0;
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    int iosurface_target=0;
#endif
#ifdef TEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE
    int present_iosurface=0;
    struct tegu_present_api present_api={0};
    struct tegu_present_hold present_hold={0};
#endif
    if (argc >= 2 && is_supervise_arg(argv[1])) return supervise_probe(argc, argv);
    if ((argc==2&&is_replay_arg(argv[1])) || (argc==3&&is_replay_arg(argv[2])))
        return replay_log(); /* No Objective-C, Metal or memory-control initialization. */
    LOG("TEGU_COMPOSITOR begin argv_count=%d\n",argc-1);
    if(argc>=3&&argv[2]&&argv[2][0]=='-'&&argv[2][1]=='-') {
        if(!__builtin_strcmp(argv[2],"--iosurface-target")) {
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
            iosurface_target=1;
            int next_arg=3;
            if(argc>next_arg&&argv[next_arg]&&__builtin_strcmp(argv[next_arg],"--present-iosurface")==0) {
#ifdef TEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE
                present_iosurface=1;next_arg++;
#else
                LOG("TEGU_COMPOSITOR present_iosurface_not_built\n");return terminal_verdict(2);
#endif
            }
            if(argc!=next_arg&&argc!=next_arg+1) {
                LOG("usage: probe metallib --iosurface-target [--present-iosurface] [probe-limit-MiB]\n");return terminal_verdict(2);
            }
            if(argc==next_arg+1&&parse_probe_memory(argv[next_arg],&requested_memory)) {
                LOG("TEGU_COMPOSITOR invalid probe memory limit\n");return terminal_verdict(2);
            }
#else
            LOG("TEGU_COMPOSITOR iosurface_probe_not_built\n");return terminal_verdict(2);
#endif
        } else {
#ifdef TEGU_COMPOSITOR_LOAD_PROBE
            if((argc!=4&&argc!=5)||__builtin_strcmp(argv[2],"--compositor-load-two-draws")||
               tegu_public_parse_alpha(argv[3],&source_alpha)) {
                LOG("usage: probe metallib --compositor-load-two-draws alpha [probe-limit-MiB]\n");return terminal_verdict(2);
            }
            load_two_draws=1;
#ifdef TEGU_LOAD_SOURCE_ALPHA
            if(source_alpha!=TEGU_LOAD_SOURCE_ALPHA) {LOG("TEGU_COMPOSITOR alpha_must_match_built_oracle expected=%u actual=%u\n",TEGU_LOAD_SOURCE_ALPHA,source_alpha);return terminal_verdict(2);}
#endif
            source_alpha_second=255-source_alpha;
            if(argc==5&&parse_probe_memory(argv[4],&requested_memory)) {LOG("TEGU_COMPOSITOR invalid probe memory limit\n");return terminal_verdict(2);}
#else
            LOG("TEGU_COMPOSITOR load_probe_not_built\n");return terminal_verdict(2);
#endif
        }
    } else {
        if (argc != 2 && argc != 3) { LOG("usage: tegu-compositor-public-probe /path/to/QuartzCore.metallib [probe-limit-MiB]\n"); return terminal_verdict(2); }
        if (argc==3&&parse_probe_memory(argv[2],&requested_memory)) { LOG("TEGU_COMPOSITOR invalid probe memory limit\n");return terminal_verdict(2); }
    }
    struct tegu_public_draw_plan draw_plan;
    if(tegu_public_make_draw_plan(load_two_draws,&draw_plan)){LOG("TEGU_COMPOSITOR draw_plan_invalid\n");return terminal_verdict(2);}
    if(probe_log_failed)return terminal_verdict(3);
#ifdef TEGU_COMPOSITOR_DIRECT_BUNDLE
    LOG("TEGU_COMPOSITOR device_mode=direct_bundle path=%s\n",TEGU_COMPOSITOR_BUNDLE_PATH);
    if(probe_log_failed)return terminal_verdict(3);
#endif
    /* Apply the explicit diagnostic budget before loading either framework.
     * The direct, unforked launch starts with launchd's small default cap. */
    object system = dlopen("/usr/lib/libSystem.B.dylib", 2);
    if (configure_probe_memory(system,requested_memory)||probe_log_failed) { LOG("TEGU_COMPOSITOR FAILED memory_limit_setup_or_log\n"); return terminal_verdict(3); }
    object objc = dlopen("/usr/lib/libobjc.A.dylib", 2);
    object metal = dlopen("/System/Library/Frameworks/Metal.framework/Metal", 2);
    object foundation = dlopen("/System/Library/Frameworks/Foundation.framework/Foundation", 2);
    register_selector = objc ? (selector (*)(const char *))dlsym(objc, "sel_registerName") : 0;
    send_message = objc ? dlsym(objc, "objc_msgSend") : 0;
    object (*get_class)(const char *) = objc ? (object (*)(const char *))dlsym(objc, "objc_getClass") : 0;
    void *(*pool_push)(void) = objc ? (void *(*)(void))dlsym(objc, "objc_autoreleasePoolPush") : 0;
    void (*pool_pop)(void *) = objc ? (void (*)(void *))dlsym(objc, "objc_autoreleasePoolPop") : 0;
#ifdef TEGU_COMPOSITOR_DIRECT_BUNDLE
    object (*get_superclass)(object) = objc ? (object (*)(object))dlsym(objc, "class_getSuperclass") : 0;
#else
    object (*create_device)(void) = metal ? (object (*)(void))dlsym(metal, "MTLCreateSystemDefaultDevice") : 0;
#ifdef TEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY
    object (*object_class)(object) = objc ? (object (*)(object))dlsym(objc, "object_getClass") : 0;
    object (*get_superclass)(object) = objc ? (object (*)(object))dlsym(objc, "class_getSuperclass") : 0;
#endif
#endif
    object (*data_create)(const void *, usize, object, object) = system ? (object (*)(const void *, usize, object, object))dlsym(system, "dispatch_data_create") : 0;
    void (*data_release)(object) = system ? (void (*)(object))dlsym(system, "dispatch_release") : 0;
    if (!objc || !metal || !foundation || !system || !register_selector || !send_message || !get_class || !pool_push || !pool_pop ||
#ifdef TEGU_COMPOSITOR_DIRECT_BUNDLE
        !get_superclass ||
#else
        !create_device ||
#ifdef TEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY
        !object_class || !get_superclass ||
#endif
#endif
        !data_create || !data_release) {
        char *reason=dlerror();LOG("TEGU_COMPOSITOR runtime symbol missing: %s\n",reason?reason:"unknown"); return terminal_verdict(3);
    }
    void *pool = pool_push();
    void *library_bytes = 0; usize library_length = 0;
    if (read_library_file(argv[1], &library_bytes, &library_length)) {
        LOG("TEGU_COMPOSITOR library-read failed path=%s\n", argv[1]); pool_pop(pool); return terminal_verdict(4);
    }
    LOG("TEGU_COMPOSITOR library-read bytes=%lu source=%s\n", library_length, argv[1]);
    object device = 0, library_data = 0, library = 0;
    object vertex_name = 0, fragment_name = 0, vertex = 0, fragment = 0;
    object descriptor = 0, vertex_layout = 0, pipeline = 0;
    object source_descriptor = 0, source = 0, source_second = 0, sampler_descriptor = 0, sampler = 0;
    object vertex_buffer = 0, ubo_vs2 = 0, ubo_vs3 = 0, ubo_fs1 = 0, ubo_fs6 = 0;
    object target = 0;
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    object target_surface=0;
    struct tegu_public_iosurface_api surface_api={0};
#endif
    object queue = 0, pass = 0, command = 0, encoder = 0, error = 0;
    void *(*dispatch_alloc)(usize) = system ? (void *(*)(usize))dlsym(system, "malloc") : 0;
    void (*dispatch_free)(void *) = system ? (void (*)(void *))dlsym(system, "free") : 0;
    int rc = 1;
    if (probe_log_failed || !dispatch_alloc || !dispatch_free) { LOG("TEGU_COMPOSITOR no_allocator_or_log\n"); goto cleanup; }
#ifdef TEGU_COMPOSITOR_DIRECT_BUNDLE
    device=tegu_create_direct_bundle_device(foundation,get_class,get_superclass);
#else
    LOG("TEGU_COMPOSITOR phase device_create begin\n");
    if (probe_log_failed) goto cleanup;
    device = create_device();
    LOG("TEGU_COMPOSITOR phase device_create returned present=%d\n", device != 0);
#ifdef TEGU_COMPOSITOR_VERIFY_SYSTEM_DEVICE_IDENTITY
    if (device && !probe_log_failed) {
        struct tegu_system_device_identity identity;
        struct tegu_system_device_identity_ops ops = {
            get_class("TeguComputeDevice"), get_class("_MTLDevice"),
            (void *(*)(void *))object_class,
            (void *(*)(void *))get_superclass,
            tegu_system_registry_id, (void *(*)(void))create_device,
            tegu_system_release
        };
        int identity_rc = tegu_verify_system_device_identity(device, &ops, &identity);
        LOG("TEGU_COMPOSITOR system_device_identity class=%d superclass_mtl_device=%d registry_id_nonzero=%d repeated_default_same_object=%d result=%s\n",
            identity.class_match, identity.superclass_match,
            identity.registry_id_nonzero, identity.repeated_discovery_same_object,
            identity_rc ? "FAIL" : "PASS");
        if (identity_rc || probe_log_failed) goto cleanup;
    }
#endif
#endif
    if (probe_log_failed || !device) { if (!probe_log_failed&&!device) LOG("TEGU_COMPOSITOR no_device\n"); goto cleanup; }
    LOG("TEGU_COMPOSITOR phase dispatch_data_create begin bytes=%lu\n", library_length);
    if (probe_log_failed) goto cleanup;
    library_data = data_create(library_bytes, library_length, 0, 0);
    LOG("TEGU_COMPOSITOR phase dispatch_data_create returned present=%d\n", library_data != 0);
    if (probe_log_failed) goto cleanup;
    LOG("TEGU_COMPOSITOR phase newLibraryWithData begin\n");
    if (probe_log_failed) goto cleanup;
    library = library_data ? ((object (*)(object, selector, object, object *))send_message)
        (device, SEL("newLibraryWithData:error:"), library_data, &error) : 0;
    log_error("library", error); error = 0;
    LOG("TEGU_COMPOSITOR phase newLibraryWithData returned present=%d\n", library != 0);
    if (probe_log_failed) goto cleanup;
    if (!library) goto cleanup;
    LOG("TEGU_COMPOSITOR library_opened\n");
    if(probe_log_failed)goto cleanup;
    vertex_name = retained(((object (*)(object, selector, const char *))send_message)(get_class("NSString"), SEL("stringWithUTF8String:"), "downsample_blur_vert_lph"));
    fragment_name = retained(((object (*)(object, selector, const char *))send_message)(get_class("NSString"), SEL("stringWithUTF8String:"), TEGU_PUBLIC_FRAGMENT_NAME));
    vertex = MSG1(library, "newFunctionWithName:", vertex_name);
    fragment = MSG1(library, "newFunctionWithName:", fragment_name);
    if (!vertex || !fragment) { LOG("TEGU_COMPOSITOR function lookup vertex=%d fragment=%d\n", vertex != 0, fragment != 0); goto cleanup; }
    LOG("TEGU_COMPOSITOR functions_found vertex=downsample_blur_vert_lph fragment=%s\n",TEGU_PUBLIC_FRAGMENT_NAME);
    if(probe_log_failed)goto cleanup;

    descriptor = MSG0(get_class("MTLRenderPipelineDescriptor"), "new");
    if (!descriptor) goto cleanup;
    ((void (*)(object, selector, object))send_message)(descriptor, SEL("setVertexFunction:"), vertex);
    ((void (*)(object, selector, object))send_message)(descriptor, SEL("setFragmentFunction:"), fragment);
    MSGU(descriptor, "setRasterSampleCount:", 1);
    MSGU(descriptor, "setMaxVertexAmplificationCount:", 1);
    MSGU(descriptor, "setInputPrimitiveTopology:", 3);
    object colors = MSG0(descriptor, "colorAttachments");
    for (unsigned i = 0; i < 8; ++i) {
        object a = ((object (*)(object, selector, usize))send_message)(colors, SEL("objectAtIndexedSubscript:"), i);
        MSGU(a, "setPixelFormat:", i < 2 ? 80 : 0);
        MSGU(a, "setWriteMask:", i == 0 ? 15 : 0);
        if(load_two_draws&&i==0) {
            MSGI(a,"setBlendingEnabled:",1);MSGU(a,"setRgbBlendOperation:",0);
            MSGU(a,"setSourceRGBBlendFactor:",1);MSGU(a,"setDestinationRGBBlendFactor:",5);
            MSGU(a,"setAlphaBlendOperation:",0);MSGU(a,"setSourceAlphaBlendFactor:",1);
            MSGU(a,"setDestinationAlphaBlendFactor:",5);
        }
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
        if(i==0) {
            MSGI(a,"setBlendingEnabled:",1);MSGU(a,"setRgbBlendOperation:",0);
            MSGU(a,"setSourceRGBBlendFactor:",1);MSGU(a,"setDestinationRGBBlendFactor:",5);
            MSGU(a,"setAlphaBlendOperation:",0);MSGU(a,"setSourceAlphaBlendFactor:",1);
            MSGU(a,"setDestinationAlphaBlendFactor:",5);
        }
#endif
    }
    vertex_layout = retained(MSG0(get_class("MTLVertexDescriptor"), "vertexDescriptor"));
    object attrs = MSG0(vertex_layout, "attributes");
    object layouts = MSG0(vertex_layout, "layouts");
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    static const unsigned formats[4]={31,29,29,27},offsets[4]={0,16,24,32};
    for(unsigned i=0;i<4;i++) {
        object attr=((object (*)(object,selector,usize))send_message)(attrs,SEL("objectAtIndexedSubscript:"),i);
        MSGU(attr,"setFormat:",formats[i]);MSGU(attr,"setOffset:",offsets[i]);MSGU(attr,"setBufferIndex:",1);
    }
    object l1=((object (*)(object,selector,usize))send_message)(layouts,SEL("objectAtIndexedSubscript:"),1);
    MSGU(l1,"setStride:",48);MSGU(l1,"setStepFunction:",1);MSGU(l1,"setStepRate:",1);
#else
    object a0 = ((object (*)(object, selector, usize))send_message)(attrs, SEL("objectAtIndexedSubscript:"), 0);
    object a1 = ((object (*)(object, selector, usize))send_message)(attrs, SEL("objectAtIndexedSubscript:"), 1);
    MSGU(a0, "setFormat:", 31); MSGU(a0, "setOffset:", 0); MSGU(a0, "setBufferIndex:", 0);
    MSGU(a1, "setFormat:", 29); MSGU(a1, "setOffset:", 16); MSGU(a1, "setBufferIndex:", 0);
    object l0 = ((object (*)(object, selector, usize))send_message)(layouts, SEL("objectAtIndexedSubscript:"), 0);
    MSGU(l0, "setStride:", 40); MSGU(l0, "setStepFunction:", 1); MSGU(l0, "setStepRate:", 1);
#endif
    ((void (*)(object, selector, object))send_message)(descriptor, SEL("setVertexDescriptor:"), vertex_layout);
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    LOG("TEGU_COMPOSITOR pso_compile_begin fragment=downsample_8_frag_lph attachments=0:80/15,1:80/0,2-7:0/0 blend=ADD One OneMinusSourceAlpha/alpha ADD One OneMinusSourceAlpha vertex=UI-layout attrs=0:Float4@0,1:Float2@16,2:Float2@24,3:Half4@32,buffer1,stride48,PerVertex\n");
#else
    if(load_two_draws)
        LOG("TEGU_COMPOSITOR pso_compile_begin attachments=0:80/15,1:80/0,2-7:0/0 vertex=Float4@0,Float2@16,index0,stride40,PerVertex blend=ADD One OneMinusSourceAlpha / alpha ADD One OneMinusSourceAlpha\n");
    else LOG("TEGU_COMPOSITOR pso_compile_begin attachments=0:80/15,1:80/0,2-7:0/0 vertex=Float4@0,Float2@16,index0,stride40,PerVertex\n");
#endif
    if(probe_log_failed)goto cleanup;
    pipeline = ((object (*)(object, selector, object, object *))send_message)
        (device, SEL("newRenderPipelineStateWithDescriptor:error:"), descriptor, &error);
    log_error("pso", error); error = 0;
    if (!pipeline) goto cleanup;
    LOG("TEGU_COMPOSITOR pso_compiled\n");
    if(probe_log_failed)goto cleanup;

    /* Prefixes position each reflected UBO at its nonzero Metal binding offset. */
    unsigned char b_vs2[80] = {0}, b_vs3[32] = {0};
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    unsigned char b_fs1_8tap[72]={0},b_fs6_8tap[32]={0};
#else
    unsigned char b_fs1[96] = {0}, b_fs6[32] = {0};
#endif
    for (unsigned i = 0; i < 64; ++i) b_vs2[16 + i] = tegu_vertex_vs2[i];
    for (unsigned i = 0; i < 16; ++i) b_vs3[16 + i] = tegu_vertex_vs3[i];
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    /* Full Metal buffer1 descriptor is 48 bytes at offset24. Its first 32
     * bytes are the four float2 offsets; all are zero so the constant source
     * texel makes every one of the fragment's 16 coordinates equivalent. */
    for(unsigned i=0;i<2;i++)b_fs6_8tap[30+i]=tegu_fragment_fs6[i];
    struct tegu_public_vertex48 vertices_8tap[3]={
        {{-1.0f,-1.0f,0.5f,1.0f},{0.0f,0.0f},{0.0f,0.0f},{0x3c00,0x3c00,0x3c00,0x3c00},{0}},
        {{ 3.0f,-1.0f,0.5f,1.0f},{1.0f,0.0f},{0.0f,0.0f},{0x3c00,0x3c00,0x3c00,0x3c00},{0}},
        {{-1.0f, 3.0f,0.5f,1.0f},{0.0f,1.0f},{0.0f,0.0f},{0x3c00,0x3c00,0x3c00,0x3c00},{0}}
    };
    vertex_buffer=((object (*)(object,selector,const void *,usize,usize))send_message)
        (device,SEL("newBufferWithBytes:length:options:"),vertices_8tap,sizeof(vertices_8tap),0);
    ubo_vs2=((object (*)(object,selector,const void *,usize,usize))send_message)(device,SEL("newBufferWithBytes:length:options:"),b_vs2,sizeof(b_vs2),0);
    ubo_vs3=((object (*)(object,selector,const void *,usize,usize))send_message)(device,SEL("newBufferWithBytes:length:options:"),b_vs3,sizeof(b_vs3),0);
    ubo_fs1=((object (*)(object,selector,const void *,usize,usize))send_message)(device,SEL("newBufferWithBytes:length:options:"),b_fs1_8tap,sizeof(b_fs1_8tap),0);
    ubo_fs6=((object (*)(object,selector,const void *,usize,usize))send_message)(device,SEL("newBufferWithBytes:length:options:"),b_fs6_8tap,sizeof(b_fs6_8tap),0);
#else
    for (unsigned i = 0; i < 72; ++i) b_fs1[24 + i] = tegu_fragment_fs1[i];
    for (unsigned i = 0; i < 2; ++i) b_fs6[30 + i] = tegu_fragment_fs6[i];
    vertex_buffer = ((object (*)(object, selector, const void *, usize, usize))send_message)
        (device, SEL("newBufferWithBytes:length:options:"), tegu_vertices_stride40, sizeof(tegu_vertices_stride40), 0);
    ubo_vs2 = ((object (*)(object, selector, const void *, usize, usize))send_message)(device, SEL("newBufferWithBytes:length:options:"), b_vs2, sizeof(b_vs2), 0);
    ubo_vs3 = ((object (*)(object, selector, const void *, usize, usize))send_message)(device, SEL("newBufferWithBytes:length:options:"), b_vs3, sizeof(b_vs3), 0);
    ubo_fs1 = ((object (*)(object, selector, const void *, usize, usize))send_message)(device, SEL("newBufferWithBytes:length:options:"), b_fs1, sizeof(b_fs1), 0);
    ubo_fs6 = ((object (*)(object, selector, const void *, usize, usize))send_message)(device, SEL("newBufferWithBytes:length:options:"), b_fs6, sizeof(b_fs6), 0);
#endif
    if (!vertex_buffer || !ubo_vs2 || !ubo_vs3 || !ubo_fs1 || !ubo_fs6) { LOG("TEGU_COMPOSITOR buffer_allocation_failed\n"); goto cleanup; }
    source_descriptor = make_texture_descriptor(get_class("MTLTextureDescriptor"), send_message, register_selector, 70, 1);
    source = MSG1(device, "newTextureWithDescriptor:", source_descriptor);
    if(load_two_draws)source_second=MSG1(device,"newTextureWithDescriptor:",source_descriptor);
    if (!source || (load_two_draws&&!source_second)) goto cleanup;
    struct region source_region = {{0,0,0},{16,16,1}};
    unsigned char source_pixels[1024],source_pixels_second[1024];
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    const unsigned char source_rgba_8tap[4]={64,128,192,255};
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<4;c++)
        source_pixels[i*4+c]=source_rgba_8tap[c];
#else
    if(load_two_draws) {
        unsigned char r=(unsigned char)(source_alpha/2),g=(unsigned char)(source_alpha/4),b=(unsigned char)(source_alpha/8);
        unsigned char r2=(unsigned char)(source_alpha_second/8),g2=(unsigned char)(source_alpha_second/2),b2=(unsigned char)(source_alpha_second/4);
        for(unsigned i=0;i<256;i++){
            source_pixels[4*i]=r;source_pixels[4*i+1]=g;source_pixels[4*i+2]=b;source_pixels[4*i+3]=(unsigned char)source_alpha;
            source_pixels_second[4*i]=r2;source_pixels_second[4*i+1]=g2;source_pixels_second[4*i+2]=b2;source_pixels_second[4*i+3]=(unsigned char)source_alpha_second;
        }
    } else for(unsigned i=0;i<sizeof(source_pixels);i++)source_pixels[i]=tegu_source_rgba8[i];
#endif
    ((void (*)(object, selector, struct region, usize, const void *, usize))send_message)
        (source, SEL("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), source_region, 0, source_pixels, 64);
    if(load_two_draws)((void (*)(object, selector, struct region, usize, const void *, usize))send_message)
        (source_second, SEL("replaceRegion:mipmapLevel:withBytes:bytesPerRow:"), source_region, 0, source_pixels_second, 64);
    unsigned char source_readback[1026],source_readback_second[1026];
    for (unsigned i = 0; i < sizeof(source_readback); ++i) source_readback[i] = 0xa5;
    ((void (*)(object, selector, void *, usize, struct region, usize))send_message)
        (source, SEL("getBytes:bytesPerRow:fromRegion:mipmapLevel:"), source_readback + 1, 64, source_region, 0);
    unsigned source_bad = source_readback[0] != 0xa5 || source_readback[1025] != 0xa5;
    for (unsigned i = 0; i < 1024; ++i) source_bad += source_readback[1 + i] != source_pixels[i];
    if(load_two_draws) {
        for (unsigned i = 0; i < sizeof(source_readback_second); ++i) source_readback_second[i] = 0xa5;
        ((void (*)(object, selector, void *, usize, struct region, usize))send_message)
            (source_second, SEL("getBytes:bytesPerRow:fromRegion:mipmapLevel:"), source_readback_second + 1, 64, source_region, 0);
        source_bad += source_readback_second[0] != 0xa5 || source_readback_second[1025] != 0xa5;
        for (unsigned i = 0; i < 1024; ++i) source_bad += source_readback_second[1 + i] != source_pixels_second[i];
    }
    if (source_bad) { LOG("TEGU_COMPOSITOR source_upload_mismatch=%u\n", source_bad); goto cleanup; }

    sampler_descriptor = MSG0(get_class("MTLSamplerDescriptor"), "new");
    MSGU(sampler_descriptor, "setMinFilter:", 0); MSGU(sampler_descriptor, "setMagFilter:", 0);
    MSGU(sampler_descriptor, "setMipFilter:", 0); MSGU(sampler_descriptor, "setSAddressMode:", 0);
    MSGU(sampler_descriptor, "setTAddressMode:", 0); MSGU(sampler_descriptor, "setRAddressMode:", 0);
    MSGI(sampler_descriptor, "setNormalizedCoordinates:", 1); MSGU(sampler_descriptor, "setCompareFunction:", 0);
    MSGU(sampler_descriptor, "setMaxAnisotropy:", 1); MSGF(sampler_descriptor, "setLodMinClamp:", 0);
    MSGF(sampler_descriptor, "setLodMaxClamp:", 0);
    sampler = MSG1(device, "newSamplerStateWithDescriptor:", sampler_descriptor);
    if (!sampler) goto cleanup;

    /* The positive pass binds only target 0, matching selector16's supported
     * compositor request. The PSO descriptor still preserves attachment 1. */
    object target_descriptor = make_texture_descriptor(get_class("MTLTextureDescriptor"), send_message, register_selector, 80, 4);
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    if(iosurface_target) {
        uint32_t sid=0;int surface_error=tegu_iosurface_target_create(get_class,device,target_descriptor,
            &target_surface,&target,&sid,&surface_api);
        if(surface_error) {safe_release(target_descriptor);LOG("TEGU_COMPOSITOR iosurface_target_create_failed stage=%d\n",surface_error);goto cleanup;}
        LOG("TEGU_COMPOSITOR iosurface_target imported id=%u width=%u height=%u row_bytes=%u offset=%u alloc=%u plane_count=0\n",
            sid,TEGU_PUBLIC_SURFACE_WIDTH,TEGU_PUBLIC_SURFACE_HEIGHT,
            TEGU_PUBLIC_SURFACE_ROW_BYTES,TEGU_PUBLIC_SURFACE_OFFSET,(unsigned)surface_api.alloc_bytes(target_surface));
    } else
#endif
    target = MSG1(device, "newTextureWithDescriptor:", target_descriptor);
    safe_release(target_descriptor);
    queue = MSG0(device, "newCommandQueue");
    pass = retained(MSG0(get_class("MTLRenderPassDescriptor"), "renderPassDescriptor"));
    object pass_colors = MSG0(pass, "colorAttachments");
    object pass0 = ((object (*)(object, selector, usize))send_message)(pass_colors, SEL("objectAtIndexedSubscript:"), 0);
    object pass1 = ((object (*)(object, selector, usize))send_message)(pass_colors, SEL("objectAtIndexedSubscript:"), 1);
    ((void (*)(object, selector, object))send_message)(pass0, SEL("setTexture:"), target);
    MSGU(pass0, "setLoadAction:", 2); MSGU(pass0, "setStoreAction:", 1);
    (void)pass1; /* Intentionally unbound: the zero-mask attachment stays metadata-only. */
    struct clear_color clear = load_two_draws?(struct clear_color){40.0/255.0,80.0/255.0,120.0/255.0,128.0/255.0}:
        (struct clear_color){(double)TEGU_PROOF_CLEAR_R/255.0,(double)TEGU_PROOF_CLEAR_G/255.0,
                             (double)TEGU_PROOF_CLEAR_B/255.0,(double)TEGU_PROOF_CLEAR_A/255.0};
    ((void (*)(object, selector, struct clear_color))send_message)(pass0, SEL("setClearColor:"), clear);
    command = retained(MSG0(queue, "commandBuffer"));
    encoder = command ? retained(MSG1(command, "renderCommandEncoderWithDescriptor:", pass)) : 0;
    if (!target || !queue || !pass || !command || !encoder) { LOG("TEGU_COMPOSITOR encoder_setup_failed\n"); goto cleanup; }
    ((void (*)(object, selector, object))send_message)(encoder, SEL("setRenderPipelineState:"), pipeline);
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setVertexBuffer:offset:atIndex:"), vertex_buffer, 0, 1);
#else
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setVertexBuffer:offset:atIndex:"), vertex_buffer, 0, 0);
#endif
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setVertexBuffer:offset:atIndex:"), ubo_vs2, 16, 2);
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setVertexBuffer:offset:atIndex:"), ubo_vs3, 16, 3);
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setFragmentBuffer:offset:atIndex:"), ubo_fs1, 24, 1);
    ((void (*)(object, selector, object, usize, usize))send_message)(encoder, SEL("setFragmentBuffer:offset:atIndex:"), ubo_fs6, 30, 6);
    ((void (*)(object, selector, object, usize))send_message)(encoder, SEL("setFragmentTexture:atIndex:"), source, 3);
    ((void (*)(object, selector, object, usize))send_message)(encoder, SEL("setFragmentSamplerState:atIndex:"), sampler, 0);
    if(probe_log_failed)goto cleanup;
    for(uint32_t draw=0;draw<draw_plan.count;draw++) {
        if(load_two_draws&&draw_plan.texture_snapshot[draw])
            ((void (*)(object, selector, object, usize))send_message)(encoder,SEL("setFragmentTexture:atIndex:"),source_second,3);
        ((void (*)(object, selector, usize, usize, usize))send_message)(encoder, SEL("drawPrimitives:vertexStart:vertexCount:"), 3, 0, 3);
    }
    MSG0(encoder, "endEncoding"); safe_release(encoder); encoder = 0;
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    if(iosurface_target)
        LOG("TEGU_COMPOSITOR draw_submitted target=IOSurface selector=17 target_bytes=BGRA16x16 row_bytes=128 offset=0 store=1\n");
    else
#endif
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    LOG("TEGU_COMPOSITOR draw_submitted class=8tap samples=16 target0=BGRA8/blend_one_one-minus-src-alpha target1=BGRA8/write_mask0 bound=target0 vertex_buffer1_stride48 fs_buffer1_offset24_declared48_typed_window32 fs6_offset30_range2\n");
#else
    if(load_two_draws)
        LOG("TEGU_COMPOSITOR draw_submitted mode=clear_then_LOAD draw_count=2 sequence=%u,LOAD texture3_snapshots=A,B alpha=%u,%u bindings=vs[0,2,3] fs[1,6] sampler0 offsets=16/16/24/30 rangeLengths=80/32/96/32 attachments=2\n",
            draw_plan.action[0],source_alpha,source_alpha_second);
    else LOG("TEGU_COMPOSITOR draw_submitted bindings=vs[0,2,3] fs[1,6] texture3 sampler0 offsets=16/16/24/30 rangeLengths=80/32/96/32 attachments=2\n");
#endif
    MSG0(command, "commit"); MSG0(command, "waitUntilCompleted");
    usize status = (usize)MSG0(command, "status"); error = MSG0(command, "error");
    LOG("TEGU_COMPOSITOR completion status=%lu error_present=%d\n", status, error != 0);
    if (error) log_error("command", error);
    if (status != 4 || error) goto cleanup;
    unsigned mismatches = 0,covered_mismatches=0,clear_mismatches=0,covered_pixels=0,clear_pixels=0;
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    unsigned char expected_8tap[TEGU_8TAP_TARGET_PIXELS*4];
    const unsigned char clear_rgba_8tap[4]={TEGU_PROOF_CLEAR_R,TEGU_PROOF_CLEAR_G,
        TEGU_PROOF_CLEAR_B,TEGU_PROOF_CLEAR_A};
    if(tegu_8tap_expected_constant_opaque(source_rgba_8tap,clear_rgba_8tap,expected_8tap)) {
        LOG("TEGU_COMPOSITOR 8tap_cpu_oracle_rejected_input\n");goto cleanup;
    }
#endif
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    if(iosurface_target) {
        usize actual_alloc=surface_api.alloc_bytes(target_surface);
        if(actual_alloc!=TEGU_PUBLIC_SURFACE_REQUEST_ALLOC_BYTES) {LOG("TEGU_COMPOSITOR iosurface_read_allocation_changed\n");goto cleanup;}
        struct tegu_public_surface_layout layout={TEGU_PUBLIC_SURFACE_WIDTH,TEGU_PUBLIC_SURFACE_HEIGHT,
            TEGU_PUBLIC_SURFACE_ROW_BYTES,TEGU_PUBLIC_SURFACE_OFFSET,(uint32_t)actual_alloc};
        unsigned seed=0;uint32_t guard_mismatches=0;
        if(!surface_api.lock||surface_api.lock(target_surface,0,&seed)){LOG("TEGU_COMPOSITOR iosurface_read_lock_failed\n");goto cleanup;}
        /* IOSurfaceGetAllocSize() is re-read for this validation; the helper
         * indexes no farther than that actual reported allocation. */
        void *base=surface_api.base(target_surface);
        int layout_ok=base&&tegu_public_surface_verify(base,&layout,
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
            expected_8tap,
#else
            tegu_expected_bgra8,
#endif
            0xa5,
            (uint32_t *)&mismatches,&guard_mismatches);
        int unlock_bad=surface_api.unlock(target_surface,0,&seed)!=0;
        LOG("TEGU_COMPOSITOR iosurface_readback pixels=256 row_bytes=%u offset=%u alloc=%u pixel_mismatches=%u guard_mismatches=%u unlock_failed=%d\n",
            layout.row_bytes,layout.offset,layout.allocation_bytes,mismatches,guard_mismatches,unlock_bad);
        if(!layout_ok||unlock_bad||guard_mismatches) {
            LOG("TEGU_COMPOSITOR GPU_READBACK_FAIL layout=%d unlock=%d guards=%u\n",layout_ok,unlock_bad,guard_mismatches);
            goto cleanup;
        }
        if(mismatches) {
            LOG("TEGU_COMPOSITOR GPU_READBACK_FAIL pixel_mismatches=%u\n",mismatches);
            goto cleanup;
        }
        LOG("TEGU_COMPOSITOR GPU_READBACK_PASS exact_pixels=256 guards=0\n");
#ifdef TEGU_COMPOSITOR_PRESENT_IOSURFACE_PROBE
        if(present_iosurface) {
            struct tegu_present_result result={TEGU_PRESENT_API_UNAVAILABLE,0,0,0};
            if(tegu_present_api_load(&present_api,dlopen,dlsym)) {
                result=tegu_present_surface_and_readback(target_surface,128,128,16,16,
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
                    expected_8tap,TEGU_8TAP_TARGET_PIXELS*4u,
#else
                    tegu_expected_bgra8,sizeof(tegu_expected_bgra8),
#endif
                    10000,250,
                    tegu_present_pause,NULL,&present_api,&present_hold);
            }
            const char *present_class=result.status==TEGU_PRESENT_PIXELS_MATCH?"PASS":
                (result.status==TEGU_PRESENT_ALREADY_MATCHED||
                result.status==TEGU_PRESENT_TIMEOUT||result.status==TEGU_PRESENT_READBACK_ERROR)?
                "INCONCLUSIVE":"FAILED";
            LOG("TEGU_COMPOSITOR IOSURFACE_PRESENT status=%s status_code=%u classification=%s gpu_readback=PASS surface_id=%u swap_id=%u polls=%u hold_retained=%u\n",
                tegu_present_status_name(result.status),(unsigned)result.status,present_class,result.surface_id,
                result.swap_id,result.polls,present_hold.surface!=0);
            if(result.status==TEGU_PRESENT_PIXELS_MATCH) {
                if(probe_log_failed)goto cleanup;
                tegu_present_release_hold(&present_hold);
                LOG("TEGU_COMPOSITOR IOSURFACE_PANEL_PIXELS_PASS release_after_match=1\n");
                rc=probe_log_failed?1:0;
            } else {
                /* Timeout/readback errors may leave a pending SwapEnd record;
                 * the in-process hold stays alive until this inconclusive run exits. */
                LOG("TEGU_COMPOSITOR IOSURFACE_PRESENT_TERMINAL classification=%s exit_code=%u\n",
                    present_class,32u+(unsigned)result.status);
                rc=32+(int)result.status;
            }
            goto cleanup;
        }
#endif
        rc=0;
        goto cleanup;
    }
#endif
    unsigned char readback[1026]; for (unsigned i = 0; i < sizeof(readback); ++i) readback[i] = 0xa5;
    struct region target_region = {{0,0,0},{16,16,1}};
    ((void (*)(object, selector, void *, usize, struct region, usize))send_message)
        (target, SEL("getBytes:bytesPerRow:fromRegion:mipmapLevel:"), readback + 1, 64, target_region, 0);
#ifdef TEGU_COMPOSITOR_LOAD_PROBE
    const unsigned char *expected=load_two_draws?tegu_load_expected_bgra8:tegu_expected_bgra8;
    for (unsigned i = 0; i < 1024; ++i) {
        int covered=load_two_draws?tegu_load_covered_pixels[i/4]:1;
        int bad=!tegu_public_byte_matches(readback[1+i],expected[i],load_two_draws&&covered);
        mismatches+=(unsigned)bad;
        if(covered){covered_pixels+=(i%4==0);covered_mismatches+=(unsigned)bad;}
        else {clear_pixels+=(i%4==0);clear_mismatches+=(unsigned)bad;}
    }
#else
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    const unsigned char *expected=expected_8tap;
    for (unsigned i = 0; i < 1024; ++i) mismatches+=(readback[1+i]!=expected[i]);
#else
    const unsigned char *expected=tegu_expected_bgra8;
    for (unsigned i = 0; i < 1024; ++i) mismatches+=(readback[1+i]!=expected[i]);
#endif
#endif
    unsigned readback_guard_bad = readback[0] != 0xa5 || readback[1025] != 0xa5;
    if(load_two_draws)
        LOG("TEGU_COMPOSITOR verify pixels=256 covered=%u covered_mismatches=%u clear=%u clear_mismatches=%u byte_mismatches=%u cpu_readback_guard_mismatches=%u source_alpha=%u,%u\n",
            covered_pixels,covered_mismatches,clear_pixels,clear_mismatches,mismatches,readback_guard_bad,source_alpha,source_alpha_second);
#ifdef TEGU_COMPOSITOR_8TAP_PROBE
    LOG("TEGU_COMPOSITOR verify_8tap pixels=256 samples=16 source=RGBA(64,128,192,255) expected=BGRA(192,128,64,255) byte_mismatches=%u cpu_readback_guard_mismatches=%u shader_source=constant-texel-16x16\n",mismatches,readback_guard_bad);
#else
    else LOG("TEGU_COMPOSITOR verify pixels=256 byte_mismatches=%u cpu_readback_guard_mismatches=%u source_alpha=255\n",mismatches,readback_guard_bad);
#endif
    rc = mismatches || readback_guard_bad || (load_two_draws&&(covered_pixels!=66||clear_pixels!=190||clear_mismatches)) ? 1 : 0;
cleanup:
    safe_release(encoder); safe_release(command); safe_release(pass); safe_release(queue);
    safe_release(target);
#ifdef TEGU_COMPOSITOR_IOSURFACE_PROBE
    if(target_surface&&surface_api.release)surface_api.release(target_surface);
#endif
    safe_release(sampler); safe_release(sampler_descriptor); safe_release(source_second); safe_release(source); safe_release(source_descriptor);
    safe_release(ubo_fs6); safe_release(ubo_fs1); safe_release(ubo_vs3); safe_release(ubo_vs2); safe_release(vertex_buffer);
    safe_release(pipeline); safe_release(vertex_layout); safe_release(descriptor); safe_release(fragment); safe_release(vertex);
    safe_release(fragment_name); safe_release(vertex_name); safe_release(library);
    if (library_data) data_release(library_data);
    safe_release(device);
    if (dispatch_free) dispatch_free(library_bytes); else free(library_bytes);
    pool_pop(pool);
    return terminal_verdict(rc);
}
