#!/usr/bin/env python3
"""Portable regression checks for the public compositor probe diagnostics.

Run with: python3 tools/tegu/metal/test_compositor_diagnostics.py
No Apple SDK, Apple artifacts, VM, or device is used.
"""
import gzip
import hashlib
import importlib.util
import io
import json
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from contextlib import redirect_stdout
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
METAL_DIR = ROOT / 'tools/tegu/metal'
PROBE_C = METAL_DIR / 'compositor_public_probe.c'


def import_from_path(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_newc_init(data):
    name = b'init\0'
    fields = [1, 0o100755, 0, 0, 1, 0, len(data), 0, 0, 0, 0, len(name), 0]
    archive = bytearray(b'070701' + b''.join(f'{n:08x}'.encode() for n in fields) + name)
    archive.extend(bytes((-len(archive)) % 4))
    archive.extend(data)
    archive.extend(bytes((-len(archive)) % 4))
    trailer_name = b'TRAILER!!!\0'
    trailer = [0] * 13
    trailer[11] = len(trailer_name)
    archive.extend(b'070701' + b''.join(f'{n:08x}'.encode() for n in trailer) + trailer_name)
    archive.extend(bytes((-len(archive)) % 4))
    archive.extend(bytes((-len(archive)) % 512))
    return bytes(archive)


class CompositorDiagnosticsTests(unittest.TestCase):
    def test_dlopen_preflight_requires_explicit_direct_bundle_mode(self):
        builder = METAL_DIR / 'build_compositor_public_probe.py'
        completed = subprocess.run(['python3', str(builder),
            '--proof-directory', '/missing/proof', '--metallib', '/missing/metallib',
            '--work', '/missing/work', '--stubs', '/missing/stub',
            '--direct-dlopen-preflight'], capture_output=True, text=True)
        self.assertEqual(completed.returncode, 2)
        self.assertIn('--direct-dlopen-preflight requires --direct-bundle-device', completed.stderr)

    def test_direct_bundle_loader_failure_identity_and_success(self):
        with tempfile.TemporaryDirectory(prefix='tegu-direct-bundle-') as td:
            td = Path(td)
            facts = td / 'facts'; facts.mkdir()
            (facts / 'compositor_proof_facts.h').write_text('''
static const unsigned char tegu_vertex_vs2[64]={0}, tegu_vertex_vs3[16]={0};
static const unsigned char tegu_fragment_fs1[80]={0}, tegu_fragment_fs6[16]={0};
static const unsigned char tegu_source_rgba8[1024]={0}, tegu_expected_bgra8[1024]={0};
static const unsigned char tegu_vertices_stride40[120]={0};
#define TEGU_PROOF_CLEAR_R 0
#define TEGU_PROOF_CLEAR_G 0
#define TEGU_PROOF_CLEAR_B 0
#define TEGU_PROOF_CLEAR_A 255
''')
            log_path = str(td / 'direct.log')
            harness = td / 'direct_test.c'
            harness.write_text(r'''#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int mode, alloc_calls, init_calls, release_calls, load_calls;
#ifdef TEGU_TEST_PREFLIGHT
static int preflight_mode, preflight_calls, preflight_flags, preflight_error_ready, preflight_dlerror_calls;
static int preflight_calls_at_load;
static int fail_preflight_begin_log, fail_stdout_after_load, stdout_attempts;
static long diagnostic_write(int fd,const void *bytes,unsigned long length) {
    if(fail_preflight_begin_log && strstr((const char *)bytes,"dlopen_preflight begin")) {
        errno=EIO;return -1;
    }
    if(fd==1) {
        stdout_attempts++;
        if(fail_stdout_after_load && strstr((const char *)bytes,"direct_bundle load_result=1")) {
            fail_stdout_after_load=0;errno=EPIPE;return -1;
        }
    }
    return write(fd,bytes,length);
}
static void *diagnostic_dlopen(const char *,int);
static char *diagnostic_dlerror(void);
static char *read_log(void);
#endif
static void *mock_send(void *, void *, ...);
static void *mock_selector(const char *s) { return (void *)s; }
#define TEGU_LOG_PATH "__LOG_PATH__"
#define TEGU_O_WRONLY O_WRONLY
#define TEGU_O_APPEND O_APPEND
#define TEGU_O_CREAT O_CREAT
#define TEGU_GET_ERRNO errno
#define TEGU_COMPOSITOR_DIRECT_BUNDLE 1
#ifdef TEGU_TEST_PREFLIGHT
#define TEGU_COMPOSITOR_DIRECT_DLOPEN_PREFLIGHT 1
#define TEGU_WRITE diagnostic_write
#define dlopen diagnostic_dlopen
#define dlerror diagnostic_dlerror
#endif
#define main tegu_unused_probe_main
#include "__PROBE_C__"
#undef main
#ifdef TEGU_TEST_PREFLIGHT
#undef dlerror
#undef dlopen
#undef TEGU_WRITE
#endif
#define O(n) ((object)(uintptr_t)(n))
#ifdef TEGU_TEST_PREFLIGHT
static void *diagnostic_dlopen(const char *path,int flags) {
    assert(!strcmp(path,"/System/Library/Extensions/TeguMetal.bundle/TeguMetal"));
    char *log=read_log();assert(strstr(log,"dlopen_preflight begin path=/System/Library/Extensions/TeguMetal.bundle/TeguMetal"));free(log);
    preflight_calls++;preflight_flags=flags;
    if(preflight_mode==1){preflight_error_ready=1;return 0;}
    preflight_calls_at_load=preflight_calls;
    return O(40);
}
static char *diagnostic_dlerror(void) {
    preflight_dlerror_calls++;
    if(preflight_error_ready){preflight_error_ready=0;return "injected dyld failure";}
    return 0;
}
#endif
static object mock_class(const char *s) {
    if(!strcmp(s,"NSString"))return O(1);
    if(!strcmp(s,"NSBundle"))return O(2);
    if(!strcmp(s,"TeguComputeDevice"))return O(3);
    if(!strcmp(s,"_MTLDevice"))return O(4);
    return 0;
}
static object mock_superclass(object c) { return c==O(3)?O(4):O(99); }
static object mock_send(void *receiver,void *selector,...) {
    const char *sel=(const char *)selector;va_list ap;va_start(ap,selector);object result=0;
    if(!strcmp(sel,"stringWithUTF8String:")) {
        const char *s=va_arg(ap,const char *);assert(!strcmp(s,"/System/Library/Extensions/TeguMetal.bundle"));result=O(5);
    } else if(!strcmp(sel,"bundleWithPath:")) {assert(receiver==O(2)&&va_arg(ap,object)==O(5));result=O(6);
    } else if(!strcmp(sel,"loadAndReturnError:")) {
        load_calls++;
#ifdef TEGU_TEST_PREFLIGHT
        assert(preflight_calls==preflight_calls_at_load);
#endif
        object *error=va_arg(ap,object *);*error=mode==1?O(7):0;result=(object)(uintptr_t)(mode!=1);
    } else if(!strcmp(sel,"localizedDescription"))result=O(8);
    else if(!strcmp(sel,"UTF8String")) {
        if(receiver==O(8))result=(object)"injected load failure";
        else if(receiver==O(9))result=(object)"/System/Library/Extensions/TeguMetal.bundle";
        else if(receiver==O(10))result=(object)"/System/Library/Extensions/TeguMetal.bundle/TeguMetal";
    } else if(!strcmp(sel,"bundlePath"))result=O(9);
    else if(!strcmp(sel,"executablePath"))result=O(10);
    else if(!strcmp(sel,"principalClass"))result=mode==2?O(11):O(3);
    else if(!strcmp(sel,"alloc")){assert(receiver==O(3));alloc_calls++;result=O(12);}
    else if(!strcmp(sel,"init")){assert(receiver==O(12));init_calls++;result=O(13);}
    else if(!strcmp(sel,"release")){release_calls++;}
    else {fprintf(stderr,"unexpected selector %s\n",sel);assert(0);}
    va_end(ap);return result;
}
static char *read_log(void) {
    FILE *f=fopen(TEGU_LOG_PATH,"rb");assert(f);char *b=calloc(1,4096);assert(b);
    size_t n=fread(b,1,4095,f);fclose(f);b[n]=0;return b;
}
int main(void) {
    register_selector=mock_selector;send_message=mock_send;
    unlink(TEGU_LOG_PATH);mode=1;
#ifdef TEGU_TEST_PREFLIGHT
    preflight_mode=0;
#endif
    object d=tegu_create_direct_bundle_device(O(20),mock_class,mock_superclass);
    char *log=read_log();assert(!d&&alloc_calls==0&&strstr(log,"load_result=0")&&strstr(log,"injected load failure"));
#ifdef TEGU_TEST_PREFLIGHT
    assert(strstr(log,"dlopen_preflight begin path=/System/Library/Extensions/TeguMetal.bundle/TeguMetal")&&
        strstr(log,"dlopen_preflight returned present=1"));
    assert(preflight_calls==1&&preflight_flags==(TEGU_COMPOSITOR_RTLD_NOW|TEGU_COMPOSITOR_RTLD_LOCAL)&&
        tegu_direct_bundle_preflight_handle==O(40));free(log);
#else
    assert(!strstr(log,"dlopen_preflight"));free(log);
#endif
    unlink(TEGU_LOG_PATH);probe_log_failed=0;mode=2;d=tegu_create_direct_bundle_device(O(20),mock_class,mock_superclass);
    log=read_log();assert(!d&&alloc_calls==0&&strstr(log,"class_identity=0 superclass_mtl_device=0"));free(log);
    unlink(TEGU_LOG_PATH);probe_log_failed=0;probe_console_failed=0;mode=0;
#ifdef TEGU_TEST_PREFLIGHT
    fail_stdout_after_load=1;
#endif
    d=tegu_create_direct_bundle_device(O(20),mock_class,mock_superclass);
    log=read_log();assert(d==O(13)&&alloc_calls==1&&init_calls==1&&strstr(log,"load_result=1")&&
        strstr(log,"bundle_path=/System/Library/Extensions/TeguMetal.bundle")&&
        strstr(log,"executable_path=/System/Library/Extensions/TeguMetal.bundle/TeguMetal")&&
        strstr(log,"class_identity=1 superclass_mtl_device=1")&&strstr(log,"alloc_init returned present=1"));
#ifdef TEGU_TEST_PREFLIGHT
    assert(tegu_direct_bundle_preflight_handle==O(40)&&probe_console_failed&&
        strstr(log,"console_output_disabled errno=32 after=TEGU_COMPOSITOR direct_bundle load_result=1")&&
        strstr(log,"phase direct_bundle principal_class begin")&&
        strstr(log,"class_identity=1 superclass_mtl_device=1")&&
        strstr(log,"alloc_init returned present=1"));
#endif
    safe_release(d);assert(release_calls==1);free(log);unlink(TEGU_LOG_PATH);
#ifdef TEGU_TEST_PREFLIGHT
    probe_log_failed=0;probe_console_failed=0;preflight_mode=1;int prior_loads=load_calls,prior_dlerror=preflight_dlerror_calls;
    d=tegu_create_direct_bundle_device(O(20),mock_class,mock_superclass);
    log=read_log();assert(!d&&load_calls==prior_loads&&strstr(log,"dlopen_preflight returned present=0 error=injected dyld failure")&&
        preflight_dlerror_calls==prior_dlerror+2); /* clear then immediate failure read */
    free(log);unlink(TEGU_LOG_PATH);
    probe_log_failed=0;probe_console_failed=0;preflight_mode=0;fail_preflight_begin_log=1;int prior_calls=preflight_calls;
    d=tegu_create_direct_bundle_device(O(20),mock_class,mock_superclass);
    assert(!d&&probe_log_failed&&preflight_calls==prior_calls);fail_preflight_begin_log=0;
#endif
    unlink(TEGU_LOG_PATH);return 0;
}
'''.replace('__LOG_PATH__', log_path).replace('__PROBE_C__', str(PROBE_C)))
            for preflight in (False, True):
                exe = td / ('direct-preflight-test' if preflight else 'direct-legacy-test')
                command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(facts)]
                if preflight: command.append('-DTEGU_TEST_PREFLIGHT')
                subprocess.run([*command, str(harness), '-o', str(exe)], check=True)
                completed = subprocess.run([str(exe)], capture_output=True, text=True)
                self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_c_logger_errors_short_writes_eintr_and_replay_bypass(self):
        with tempfile.TemporaryDirectory(prefix='tegu-compositor-logger-') as td:
            td = Path(td)
            facts = td / 'facts'
            facts.mkdir()
            # Only array extents are needed here: replay must bypass the draw.
            (facts / 'compositor_proof_facts.h').write_text('''
static const unsigned char tegu_vertex_vs2[64]={0}, tegu_vertex_vs3[16]={0};
static const unsigned char tegu_fragment_fs1[80]={0}, tegu_fragment_fs6[16]={0};
static const unsigned char tegu_source_rgba8[1024]={0}, tegu_expected_bgra8[1024]={0};
static const unsigned char tegu_vertices_stride40[120]={0};
#define TEGU_PROOF_CLEAR_R 0
#define TEGU_PROOF_CLEAR_G 0
#define TEGU_PROOF_CLEAR_B 0
#define TEGU_PROOF_CLEAR_A 255
''')
            c_path = str(PROBE_C)
            log_path = str(td / 'probe.log')
            harness = td / 'logger_test.c'
            harness.write_text(r'''#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int interrupt_once, stdout_error_once, file_write_error_once, open_error_once;
static int fsync_error_once, close_error_once, runtime_calls, stdout_write_calls;
static int expect_flush_before_stdout, durable_ready;
static const char *first_runtime_path;
static long short_write(int fd, const void *p, unsigned long n) {
    if(fd==1&&expect_flush_before_stdout)assert(durable_ready);
    if(fd==1)stdout_write_calls++;
    if (interrupt_once) { interrupt_once=0; errno=EINTR; return -1; }
    if (fd==1 && stdout_error_once) { stdout_error_once=0; errno=EIO; return -1; }
    if (fd!=1 && file_write_error_once) { file_write_error_once=0; errno=EIO; return -1; }
    if (n>7) n=7;
    return write(fd,p,n);
}
static int injected_open(const char *path,int flags,unsigned mode) {
    if(open_error_once) {open_error_once=0;errno=EACCES;return -1;}
    return open(path,flags,mode);
}
static int injected_fsync(int fd) {
    if (fsync_error_once) { fsync_error_once=0; errno=EIO; return -1; }
    int result=fsync(fd);if(!result)durable_ready=1;return result;
}
static int injected_close(int fd) {
    if (close_error_once) { close_error_once=0; int result=close(fd); assert(result==0); errno=EIO; return -1; }
    return close(fd);
}
static void *trap_dlopen(const char *p,int f) { (void)f;if(!runtime_calls)first_runtime_path=p;runtime_calls++;return 0; }
static void *trap_dlsym(void *p,const char *s) { (void)p;(void)s;runtime_calls++;return 0; }
#define TEGU_LOG_PATH "__LOG_PATH__"
#define TEGU_OPEN injected_open
#define TEGU_O_WRONLY O_WRONLY
#define TEGU_O_APPEND O_APPEND
#define TEGU_O_CREAT O_CREAT
#define TEGU_WRITE short_write
#define TEGU_FSYNC injected_fsync
#define TEGU_CLOSE injected_close
#define TEGU_GET_ERRNO errno
#define main tegu_actual_probe_main
#define dlopen trap_dlopen
#define dlsym trap_dlsym
#include "__PROBE_C__"
#undef dlsym
#undef dlopen
#undef main
static char *read_log(void) {
    FILE *f=fopen(TEGU_LOG_PATH,"rb"); if(!f)return 0;
    char *b=calloc(1,65536); size_t n=fread(b,1,65535,f); fclose(f); b[n]=0; return b;
}
static void reset_logger(void) {
    probe_log_failed=0;probe_console_failed=0;
    first_durable_stage=DURABLE_STAGE_NONE;first_durable_errno=0;
    interrupt_once=stdout_error_once=file_write_error_once=open_error_once=0;
    fsync_error_once=close_error_once=0;
}
static void assert_last_terminal_failed(const char *log,const char *stage,int error) {
    char expected[160];
    snprintf(expected,sizeof(expected),"VERDICT FAIL durable_log stage=%s errno=%d",stage,error);
    char *failure=strstr(log,expected);assert(failure);
    char *pass=strstr(log,"PASS public Metal pair draw and CPU-readback guarded output");
    assert(!pass||pass<failure);
}
int main(void) {
    unlink(TEGU_LOG_PATH); reset_logger(); interrupt_once=1;
    expect_flush_before_stdout=1;durable_ready=0;
    assert(probe_log("TEGU_COMPOSITOR logger_record=%u\n",9)==0);
    expect_flush_before_stdout=0;
    assert(!interrupt_once && !terminal_verdict(0));
    char *b=read_log(); assert(b && strstr(b,"logger_record=9") && strstr(b,"PASS public Metal pair draw")); free(b);

    unlink(TEGU_LOG_PATH); reset_logger(); stdout_error_once=1;
    assert(probe_log("stdout_failure\n")==0);assert(probe_console_failed&&!probe_log_failed);
    int calls_after_console_failure=stdout_write_calls;
    assert(terminal_verdict(0)==0&&stdout_write_calls==calls_after_console_failure);
    b=read_log(); assert(b && strstr(b,"stdout_failure") &&
        strstr(b,"console_output_disabled errno=5 after=stdout_failure")&&
        strstr(b,"PASS public Metal pair draw") && !strstr(b,"VERDICT FAIL")); free(b);

    unlink(TEGU_LOG_PATH); reset_logger(); fsync_error_once=1;
    assert(probe_log("fsync_failure\n")<0); assert(terminal_verdict(0)!=0&&probe_log_failed);
    b=read_log(); assert(b && strstr(b,"fsync_failure"));assert_last_terminal_failed(b,"fsync",EIO);free(b);

    unlink(TEGU_LOG_PATH); reset_logger(); close_error_once=1;
    assert(probe_log("close_failure\n")<0); assert(terminal_verdict(0)!=0&&probe_log_failed);
    b=read_log(); assert(b && strstr(b,"close_failure"));assert_last_terminal_failed(b,"close",EIO);free(b);

    unlink(TEGU_LOG_PATH);reset_logger();open_error_once=1;
    assert(probe_log("open_failure\n")<0&&probe_log_failed);
    assert(terminal_verdict(0)!=0);b=read_log();assert(b);assert_last_terminal_failed(b,"open",EACCES);free(b);

    unlink(TEGU_LOG_PATH);reset_logger();file_write_error_once=1;
    assert(probe_log("write_failure\n")<0&&probe_log_failed);
    assert(terminal_verdict(0)!=0);b=read_log();assert(b);assert_last_terminal_failed(b,"write",EIO);free(b);

    /* Fail the terminal PASS flush itself; the next append succeeds and puts
     * a diagnostic failure verdict last. A later close error must not replace
     * the fsync error captured first. */
    unlink(TEGU_LOG_PATH);reset_logger();fsync_error_once=1;close_error_once=1;
    assert(terminal_verdict(0)!=0&&probe_log_failed);
    b=read_log();assert(b);assert_last_terminal_failed(b,"fsync",EIO);free(b);

    unlink(TEGU_LOG_PATH);reset_logger();close_error_once=1;
    assert(terminal_verdict(0)!=0&&probe_log_failed);
    b=read_log();assert(b);assert_last_terminal_failed(b,"close",EIO);free(b);

    unlink(TEGU_LOG_PATH); FILE *f=fopen(TEGU_LOG_PATH,"wb"); assert(f);
    fputs("previous durable record\n",f); fclose(f);
    char *args[]={"probe","--replay",0};
    assert(tegu_actual_probe_main(2,args)==0); assert(runtime_calls==0);
    /* A refused explicit budget must stop before loading Objective-C/Metal. */
    reset_logger();
    char *budget_args[]={"probe","/unused.metallib","512",0};
    assert(tegu_actual_probe_main(3,budget_args)!=0);
    assert(runtime_calls==3&&!strcmp(first_runtime_path,"/usr/lib/libSystem.B.dylib"));
    return 0;
}
'''.replace('__LOG_PATH__', log_path).replace('__PROBE_C__', c_path))
            exe = td / 'logger_test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(facts), str(harness), '-o', str(exe)], check=True)
            completed = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn('VERDICT FAIL', completed.stdout)
            self.assertIn('previous durable record', completed.stdout)

    def test_supervisor_wait_and_exit_states(self):
        with tempfile.TemporaryDirectory(prefix='tegu-compositor-supervisor-') as td:
            td = Path(td)
            facts = td / 'facts'
            facts.mkdir()
            (facts / 'compositor_proof_facts.h').write_text('''
static const unsigned char tegu_vertex_vs2[64]={0}, tegu_vertex_vs3[16]={0};
static const unsigned char tegu_fragment_fs1[80]={0}, tegu_fragment_fs6[16]={0};
static const unsigned char tegu_source_rgba8[1024]={0}, tegu_expected_bgra8[1024]={0};
static const unsigned char tegu_vertices_stride40[120]={0};
#define TEGU_PROOF_CLEAR_R 0
#define TEGU_PROOF_CLEAR_G 0
#define TEGU_PROOF_CLEAR_B 0
#define TEGU_PROOF_CLEAR_A 255
''')
            exe = td / 'supervisor_test'
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(facts), str(METAL_DIR / 'compositor_supervisor_host_test.c'),
                            '-o', str(exe)], check=True)
            completed = subprocess.run([str(exe), str(td / 'unique-probe.log')],
                                       capture_output=True, text=True)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertIn('PASS wait4 ECHILD failure fails durably', completed.stderr)
            self.assertIn('PASS stopped/unknown wait state fails closed', completed.stderr)

    def test_preparer_script_argv_for_default_offscreen_and_bundle(self):
        preparer = import_from_path('tegu_prepare_compositor_probe', METAL_DIR / 'prepare_probe_root_vm.py')
        verifier = sys.modules['verify_root_vm']
        binary = Path(tempfile.gettempdir()) / 'tegu-compositor-diagnostics-test-probe'
        # A structurally valid synthetic arm64 executable header is sufficient
        # for the builder's copy step; no executable is run.
        binary.write_bytes(structure_mach_o_probe())
        try:
            for offscreen in (False, True):
                for with_bundle in (False, True):
                    with self.subTest(offscreen=offscreen, bundle=with_bundle), tempfile.TemporaryDirectory(prefix='tegu-preparer-') as d:
                        td = Path(d)
                        build = td / 'build'; build.mkdir()
                        source = td / 'source.img'; source.write_bytes(b'synthetic-image')
                        result = {'image': str(source), 'sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
                                  'fsck_verified': True, 'setup_skip_verified': True, 'full_bringup_verified': True}
                        (build / 'source.json').write_text(json.dumps(result))
                        (build / 'result.json').write_text('{}')
                        init = b'for source in os systemos appos; do :; done\n'
                        (build / 'initramfs.cpio.gz').write_bytes(gzip.compress(make_newc_init(init)))
                        (build / 'command.json').write_text(json.dumps(['qemu', '-initrd', 'old-initrd', '-drive', 'old-drive']))
                        bundle = None
                        if with_bundle:
                            bundle = td / 'TeguMetal.bundle'; bundle.mkdir()
                            (bundle / 'TeguMetal').write_bytes(b'synthetic-macho')
                            (bundle / 'Info.plist').write_bytes(plistlib.dumps({
                                'CFBundleExecutable': 'TeguMetal', 'NSPrincipalClass': 'TeguComputeDevice'}))
                        args = ['prepare_probe_root_vm.py', '--build', str(build),
                                '--source-manifest', str(build / 'source.json'),
                                '--work', str(td / 'work'), '--output', str(td / 'output.img'),
                                '--fsck', str(td / 'fsck'), '--probe', str(binary),
                                '--probe-argument=--replay']
                        if bundle: args += ['--bundle', str(bundle)]
                        if offscreen: args += ['--offscreen-ui']
                        with redirect_stdout(io.StringIO()), mock.patch.object(sys, 'argv', args), \
                             mock.patch.object(preparer.subprocess, 'run',
                                               side_effect=self._mock_host_command(td, verifier, offscreen, with_bundle)):
                            preparer.main()
                        self.assertTrue((td / 'work/initramfs.cpio.gz').exists())
        finally:
            binary.unlink(missing_ok=True)

    @staticmethod
    def _mock_host_command(td, verifier, offscreen, with_bundle):
        def run(command, **kwargs):
            if command[0] == 'cp':
                shutil.copyfile(command[-2], command[-1])
                return subprocess.CompletedProcess(command, 0)
            if command[0] == 'qemu':
                work = td / 'work'
                archive = gzip.decompress((work / 'initramfs.cpio.gz').read_bytes())
                guest_script = next(data for _, name, data in verifier.entries(archive) if name == b'init\0').decode()
                command_line = next(line for line in guest_script.splitlines() if '/probe/install.py' in line)
                self_outer = CompositorDiagnosticsTests
                self_outer.assert_command_line(command_line, offscreen, with_bundle)
                markers = 'TEGU_METAL_ROOT_READBACK_OK\nTEGU_METAL_ROOT_OK\n'
                markers += ('TEGU_METAL_OFFSCREEN_MANIFEST_OK sha256=' + 'a' * 64 + '\n'
                            if offscreen else 'TEGU_METAL_UI_JOBS_UNCHANGED_OK\n')
                (work / 'guest.log').write_text(markers)
                return subprocess.CompletedProcess(command, 0)
            if Path(command[0]).name == 'fsck':
                kwargs['stdout'].write(b'appears to be OK')
                return subprocess.CompletedProcess(command, 0)
            raise AssertionError(f'unexpected subprocess: {command}')
        return run

    @staticmethod
    def assert_command_line(line, offscreen, bundle):
        assert '--probe-arguments-json /probe/probe-arguments.json' in line
        assert (' --offscreen-ui' in line) is offscreen
        assert (' --bundle /probe/TeguMetal.bundle' in line) is bundle


def structure_mach_o_probe():
    # MH_MAGIC_64, CPU_TYPE_ARM64, subtype ALL, MH_EXECUTE, padded header.
    return (0xFEEDFACF).to_bytes(4, 'little') + (0x0100000C).to_bytes(4, 'little') + \
        (0).to_bytes(4, 'little') + (2).to_bytes(4, 'little') + bytes(16)


if __name__ == '__main__':
    unittest.main(verbosity=2)
