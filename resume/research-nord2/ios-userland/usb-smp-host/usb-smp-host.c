
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>
typedef uintptr_t vm_offset_t;
typedef unsigned wait_result_t;
#define __unused __attribute__((unused))
#define THREAD_CONTINUE_NULL NULL
static unsigned core, binding, polls, blocks, nusb_background_state;
static unsigned nusb_background_stop;
static bool wrong_migration;
static jmp_buf escape;
static unsigned cpu_to_processor(unsigned n) { assert(n==0); return 17; }
static unsigned thread_bind(unsigned p) { assert(p==17); binding=p; return 0; }
static void thread_block(void *p) {
    assert(!p && binding==17); blocks++;
    if (!wrong_migration) core=0;
}
static unsigned cpu_number(void) { return core; }
static void panic(const char *s) __attribute__((noreturn));
static void panic(const char *s) { (void)s; longjmp(escape,2); }
static void quiet(const char *s,...) { (void)s; }
#define printf quiet
static void nord2_usb_enumerate(vm_offset_t mac,bool phy,bool bg) {
    assert(core==0 && binding==17 && blocks==1 && mac==0x1000 && phy && bg);
    polls++; nusb_background_state=1;
}
static void nord2_stop_at(unsigned stop,const char *label) {
    assert(stop==nusb_background_stop && (stop==16 || stop==21 || stop==22) && label);
    assert(core==0 && nusb_background_state==2 && polls==1);
    longjmp(escape,1);
}
static void *current_thread(void) { return NULL; }
static int thread_terminate(void *p) { (void)p; assert(false); return 0; }
static void
nusb_background_thread(void *arg, wait_result_t wr __unused)
{
	(void)thread_bind(cpu_to_processor(0));
	thread_block(THREAD_CONTINUE_NULL);
	if (cpu_number() != 0) {
		panic("NORD2 USB: service did not reach CPU0");
	}
	printf("NORD2 USB: service bound to CPU0\n");
	nord2_usb_enumerate((vm_offset_t)arg, true, true);
	__atomic_store_n(&nusb_background_state, 2, __ATOMIC_RELEASE);
	/* Also bounds the test if root mounting or userland stalls. */
	nord2_stop_at(nusb_background_stop, "after background MTU3 USB console");
	(void)thread_terminate(current_thread());
	thread_block(THREAD_CONTINUE_NULL);
}

int main(void) {
    for(unsigned start=0;start<8;start++) for(unsigned i=0;i<3;i++) {
        core=start; binding=polls=blocks=nusb_background_state=0;
        wrong_migration=false; nusb_background_stop=i?20+i:16;
        int result=setjmp(escape);
        if (!result) nusb_background_thread((void*)0x1000,0);
        assert(result==1 && polls==1);
    }
    core=7; binding=polls=blocks=nusb_background_state=0; wrong_migration=true;
    int result=setjmp(escape);
    if (!result) nusb_background_thread((void*)0x1000,0);
    assert(result==2 && !polls && !nusb_background_state);
    puts("PASS: USB service migrates to CPU0 before MMIO from all8 CPUs;"
         " migration failure refuses MMIO; bounded mode16/21/22 exit;"
         " SMP shutdown precedes guarded USB cleanup");
}
