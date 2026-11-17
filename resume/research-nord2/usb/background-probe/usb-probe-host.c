
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef uintptr_t vm_offset_t;
typedef size_t vm_size_t;
typedef int wait_result_t;
typedef uintptr_t thread_t;
static volatile unsigned nusb_background_state;
static unsigned spawned, freed, sleeps, thread_outcome, cpu_count;
static int create_error;
#define NSEC_PER_MSEC 1000000
#define THREAD_UNINT 0
#define THREAD_CONTINUE_NULL NULL
#define KERN_SUCCESS 0
static void clock_interval_to_deadline(unsigned a, unsigned b, uint64_t *c) {
    assert(a==1 && b==NSEC_PER_MSEC); *c=1;
}
static int assert_wait_deadline(void *a, int b, uint64_t c) {
    assert(a && b==THREAD_UNINT && c==1); return 0;
}
static void thread_block(void *a) {
    assert(a==THREAD_CONTINUE_NULL); sleeps++;
    nusb_background_state=thread_outcome;
}
static int kernel_thread_start(void (*f)(void *,wait_result_t), void *a, thread_t *t) {
    assert(f && a); *t=123; spawned++; return create_error;
}
static void thread_deallocate(thread_t t) { assert(t==123); freed++; }
static void nusb_background_thread(void *a, wait_result_t b) { (void)a; (void)b; assert(0); }
static void nord2_usb_enumerate(vm_offset_t mac, bool phy, bool bg) {
    (void)mac; (void)phy; (void)bg; assert(0);
}
static const uintptr_t pa[4] = {0x10001000, 0x10000000, 0x11203e00, 0x11201000};
static const size_t lengths[4] = {0x1000, 0x1000, 0x100, 0x2e00};
static uint8_t *regions[4];
static unsigned maps[4], reads[4], stopped, selected;
static int failed;
static char output[8192];
static uint32_t checked_read(vm_offset_t base, uint32_t off) {
    unsigned n;
    for (n=0; n<4 && base!=(uintptr_t)regions[n]; n++) {}
    assert(n<4 && off%4==0 && off+4<=lengths[n]);
    assert(n!=3 || off!=0x300); /* FIFO read would consume controller data. */
    reads[n]++;
    return *(volatile uint32_t *)(base+off);
}
static vm_offset_t ml_io_map(vm_offset_t address, vm_size_t len) {
    unsigned n;
    for (n=0; n<4 && address!=pa[n]; n++) {}
    assert(n<4 && len==lengths[n]);
    maps[n]++;
    return failed==(int)n ? 0 : (uintptr_t)regions[n];
}
static void PE_parse_boot_argn(const char *name, void *val, size_t size) {
    assert(size==sizeof(int));
    if (!strcmp(name,"cpus")) *(int *)val=cpu_count;
    else {
        assert(strcmp(name,"nord2stopat")==0);
        *(int *)val=selected;
    }
}
static void nord2_stop_at(unsigned point, const char *label) {
    assert((point==13 && strcmp(label,"after read-only MTU3 snapshot")==0) ||
           (point==16 && strcmp(label,"after background MTU3 USB console")==0));
    stopped++;
}
static int capture(const char *format, ...) {
    va_list ap;
    va_start(ap,format);
    size_t pos=strlen(output);
    int n=vsnprintf(output+pos,sizeof(output)-pos,format,ap);
    va_end(ap);
    assert(n>=0 && (size_t)n<sizeof(output)-pos);
    return n;
}
#define printf capture
static uint32_t
nord2_usb_read(vm_offset_t base, uint32_t offset)
{
	uint32_t value = checked_read(base, offset);
	
	return value;
}

static void nord2_usb_enumerate(vm_offset_t mac, bool prepare_phy, bool background);
static void nusb_background_thread(void *, wait_result_t);
static volatile unsigned nusb_background_state;
static int nusb_sleep_chan;

static void
nusb_sleep_ms(unsigned ms)
{
	uint64_t deadline;
	clock_interval_to_deadline(ms, NSEC_PER_MSEC, &deadline);
	(void)assert_wait_deadline(&nusb_sleep_chan, THREAD_UNINT, deadline);
	thread_block(THREAD_CONTINUE_NULL);
}

void
nord2_usb_probe(void)
{
	int stop = 0;
	PE_parse_boot_argn("nord2stopat", &stop, sizeof(stop));
	if (stop != 13 && stop != 14 && stop != 15 && stop != 16) {
		return;
	}
	if (stop == 13) {
		printf("NORD2 USB: MTU3 snapshot begin (read-only)\n");
	} else {
		printf("NORD2 USB: MTU3 probe begin, point%d\n", stop);
	}
	vm_offset_t infra = ml_io_map(0x10001000, 0x1000);
	vm_offset_t top = ml_io_map(0x10000000, 0x1000);
	if (!infra || !top) {
		printf("NORD2 USB: clock mapping failed\n");
		goto done;
	}
	uint32_t cg = nord2_usb_read(infra, 0xac);
	uint32_t cfg9 = nord2_usb_read(top, 0xa0);
	uint32_t cfg10 = nord2_usb_read(top, 0xb0);
	printf("NORD2 USB: INFRA_CG3=%08x TOP_CFG9=%08x TOP_CFG10=%08x\n",
	    cg, cfg9, cfg10);
	if ((cg & ((1u << 1) | (1u << 31))) || (cfg9 & (1u << 31)) ||
	    (cfg10 & (1u << 7))) {
		printf("NORD2 USB: clock gated; IPPC/MAC not read\n");
		goto done;
	}
	vm_offset_t ippc = ml_io_map(0x11203e00, 0x100);
	if (!ippc) {
		printf("NORD2 USB: IPPC mapping failed\n");
		goto done;
	}
	const uint16_t ipregs[] = {0, 4, 8, 0x10, 0x14, 0x18, 0x28,
	    0x30, 0x50, 0x8c, 0x98, 0xa0, 0xa4};
	for (unsigned i = 0; stop == 13 && i < sizeof(ipregs) / sizeof(ipregs[0]); i++) {
		printf("NORD2 USB: IPPC[%03x]=%08x\n", ipregs[i],
		    nord2_usb_read(ippc, ipregs[i]));
	}
	uint32_t power = nord2_usb_read(ippc, 8);
	uint32_t status = nord2_usb_read(ippc, 0x14);
	uint32_t port = nord2_usb_read(ippc, 0x50);
	if ((power & 1) || !(status & 1) || (port & 7)) {
		printf("NORD2 USB: device inactive; MAC not read\n");
		goto done;
	}
	vm_offset_t mac = ml_io_map(0x11201000, 0x2e00);
	if (!mac) {
		printf("NORD2 USB: MAC mapping failed\n");
		goto done;
	}
	if (stop == 14 || stop == 15) {
		nord2_usb_enumerate(mac, stop == 15, false);
		goto done;
	}
	if (stop == 16) {
		/* Single CPU: panic cleanup masks interrupts before taking ownership. */
		int cpus = 0;
		PE_parse_boot_argn("cpus", &cpus, sizeof(cpus));
		if (cpus != 1) {
			printf("NORD2 USB: background probe requires cpus=1\n");
			goto done;
		}
		thread_t thread;
		nusb_background_state = 0;
		if (kernel_thread_start(nusb_background_thread, (void *)mac, &thread) != KERN_SUCCESS) {
			printf("NORD2 USB: background thread creation failed\n");
			goto done;
		}
		thread_deallocate(thread);
		/* Yield so initialization completes before the RAM-root diagnostic. */
		for (unsigned i = 0; !nusb_background_state && i < 10000; i++) {
			nusb_sleep_ms(1);
		}
		if (nusb_background_state == 1) {
			printf("NORD2 USB: background console ready; continuing BSD boot\n");
			return;  /* Mount/execute the diagnostic RAM root concurrently. */
		}
		printf("NORD2 USB: background initialization failed or timed out\n");
		goto done;
	}
	/* Status registers are W1C, not read-to-clear. Never read the FIFOs. */
	const uint16_t regs[] = {0, 4, 0x80, 0x84, 0x100, 0x108,
	    0x110, 0x114, 0x118, 0x210, 0x214, 0x218,
	    0x400, 0x510, 0x514, 0x518, 0x610, 0x614, 0x618,
	    0x700, 0x704, 0x800, 0x850, 0x854, 0xc04, 0xc08,
	    0xc0c, 0xc10, 0xc84, 0x141c, 0x2404, 0x240c,
	    0x2418, 0x241c, 0x243c, 0x2460};
	for (unsigned i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
		printf("NORD2 USB: MAC[%04x]=%08x\n", regs[i],
		    nord2_usb_read(mac, regs[i]));
	}
 done:
	printf("NORD2 USB: MTU3 snapshot end\n");
	nord2_stop_at((unsigned)stop, stop == 13 ? "after read-only MTU3 snapshot" :
	    stop == 14 ? "after bounded MTU3 USB console" :
	    stop == 15 ? "after bounded MTU3 PHY USB console" :
	    "after background MTU3 USB console");
}

#undef printf
static void reset(void) {
    memset(maps,0,sizeof(maps)); memset(reads,0,sizeof(reads));
    memset(output,0,sizeof(output)); stopped=0; selected=13; failed=-1;
    spawned=freed=sleeps=0; create_error=0; cpu_count=1; thread_outcome=1;
    for (unsigned n=0;n<4;n++) {
        assert(mprotect(regions[n],0x4000,PROT_READ|PROT_WRITE)==0);
        memset(regions[n],0,0x4000);
    }
    *(uint32_t *)(regions[2]+0x14)=1;
}
static void run(void) {
    for (unsigned n=0;n<4;n++)
        assert(mprotect(regions[n],0x4000,PROT_READ)==0);
    nord2_usb_probe(); /* Any MMIO write faults, including accidental W1C. */
    if (selected!=16) assert(stopped==(selected==13));
}
int main(void) {
    for (unsigned n=0;n<4;n++) {
        regions[n]=mmap(NULL,0x4000,PROT_READ|PROT_WRITE,
                       MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        assert(regions[n]!=MAP_FAILED);
    }
    reset(); selected=12; run();
    for (unsigned n=0;n<4;n++) assert(maps[n]==0 && reads[n]==0);
    for (int n=0;n<4;n++) {
        reset(); failed=n; run();
        assert(strstr(output,"mapping failed"));
        assert(reads[n]==0);
    }
    const unsigned guards[][3]={{0,0xac,1u<<1},{0,0xac,1u<<31},
                               {1,0xa0,1u<<31},{1,0xb0,1u<<7}};
    for (unsigned n=0;n<4;n++) {
        reset();
        *(uint32_t *)(regions[guards[n][0]]+guards[n][1])=guards[n][2];
        run(); assert(!maps[2] && !maps[3] && strstr(output,"clock gated"));
    }
    const unsigned inactive[][2]={{8,1},{0x14,0},{0x50,1},{0x50,2},{0x50,4}};
    for (unsigned n=0;n<5;n++) {
        reset(); *(uint32_t *)(regions[2]+inactive[n][0])=inactive[n][1];
        run(); assert(!maps[3] && strstr(output,"device inactive"));
    }
    reset(); run();
    assert(reads[0]==1 && reads[1]==2 && reads[2]==16 && reads[3]==36);
    assert(strstr(output,"MAC[2460]") && strstr(output,"snapshot end"));
    reset(); selected=16; run();
    assert(!stopped && spawned==1 && freed==1 && sleeps==1 &&
           strstr(output,"continuing BSD boot"));
    reset(); selected=16; cpu_count=2; run();
    assert(stopped==1 && !spawned && !sleeps && strstr(output,"requires cpus=1"));
    reset(); selected=16; create_error=1; run();
    assert(stopped==1 && spawned==1 && !freed && !sleeps);
    reset(); selected=16; thread_outcome=2; run();
    assert(stopped==1 && spawned==1 && freed==1 && sleeps==1);
    reset(); selected=16; thread_outcome=0; run();
    assert(stopped==1 && spawned==1 && freed==1 && sleeps==10000);
    for (unsigned n=0;n<4;n++) assert(munmap(regions[n],0x4000)==0);
    puts("PASS: exact MTU3 probe; no MMIO writes; inactive gate, four clock gates,"
         " five device gates, all mapping failures, complete register snapshot;"
         " background ready/failure/timeout and single-CPU guard");
}
