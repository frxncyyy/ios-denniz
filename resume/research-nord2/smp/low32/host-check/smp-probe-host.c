
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NORD2_PSCI_LOW_SIZE 0x8000
#define DSB_SY 15
#define __builtin_arm_dsb(v) ((void)(v))
static bool nord2_psci_probe_active;
static volatile uint8_t *nord2_psci_low_alias;
static uint8_t scratch[NORD2_PSCI_LOW_SIZE];
static uint64_t now;
static unsigned calls, polls, transition_at;
static int32_t state[8];
static volatile uint64_t *mail_state;
static int32_t nord2_psci_call(uint64_t fn, uint64_t target, uint64_t a2, uint64_t a3) {
    assert(fn==0xc4000004 && !a2 && !a3);
    assert(target>=0x100 && target<=0x700 && !(target&255));
    calls++;
    return state[target>>8];
}
static uint64_t nord2_psci_now(void) { return now; }
static void nord2_psci_poll(void) {
    now++; polls++;
    if (transition_at && now==transition_at) {
        for (unsigned i=1; i<8; i++) state[i]=1;
        if (mail_state) *mail_state=1;
    }
}
static bool
nord2_psci_all_off(void)
{
	for (unsigned core = 1; core < 8; core++) {
		/* PSCI AFFINITY_INFO64, lowest_affinity_level=0; OFF is 1. */
		if (nord2_psci_call(0xc4000004, core << 8, 0, 0) != 1) {
			return false;
		}
	}
	return true;
}

int
nord2_smp_prepare_return(void)
{
	if (!nord2_psci_probe_active) {
		return 0;
	}
	uint64_t deadline = nord2_psci_now() +
	    1 * 5;
	do {
		if (nord2_psci_all_off()) {
			/* Restore the zero padding only after every secondary is OFF. */
			if (nord2_psci_low_alias) {
				for (unsigned i = 0; i < NORD2_PSCI_LOW_SIZE; i++) {
					nord2_psci_low_alias[i] = 0;
				}
				__builtin_arm_dsb(DSB_SY);
				nord2_psci_low_alias = NULL;
			}
			nord2_psci_probe_active = false;
			return 1;
		}
		nord2_psci_poll();
	} while (nord2_psci_now() < deadline);
	return -1; /* Do not release memory or chain back with a running core. */
}

/* 128-byte slots; offsets 0..72 match the stackless physical entry in start.s. */
struct nord2_psci_mailbox {
	uint64_t magic, el, mpidr, midr, sctlr, frequency, ticks, context;
	uint64_t off_result, state;
	uint64_t padding[6];
};
_Static_assert(sizeof(struct nord2_psci_mailbox) == 128, "PSCI mailbox layout");

static bool
nord2_psci_wait_off(volatile struct nord2_psci_mailbox *box,
    unsigned core, uint64_t deadline)
{
	do {
		int32_t affinity = nord2_psci_call(0xc4000004, core << 8, 0, 0);
		
		if (affinity == 1 && box->state == 1) {
			return true;
		}
		if (affinity < 0 || box->state == 2) {
			return false;
		}
		nord2_psci_poll();
	} while (nord2_psci_now() < deadline);
	return false;
}

static void reset(void) {
    nord2_psci_probe_active=true; now=0; calls=polls=transition_at=0;
    mail_state=NULL; nord2_psci_low_alias=NULL;
    for (unsigned i=0; i<8; i++) state[i]=1;
}
int main(void) {
    reset(); nord2_psci_probe_active=false;
    assert(nord2_smp_prepare_return()==0 && !calls);
    reset();
    assert(nord2_smp_prepare_return()==1 && !nord2_psci_probe_active && calls==7);
    /* Any live, pending, or unknown affinity prevents Linux chain-back. */
    for (unsigned core=1; core<8; core++) {
        const int32_t bad[]={0,2,-1,-2,-6};
        for (unsigned j=0;j<sizeof(bad)/sizeof(bad[0]);j++) {
            reset(); state[core]=bad[j];
            assert(nord2_smp_prepare_return()==-1 && nord2_psci_probe_active && now==5);
        }
    }
    reset(); state[7]=2; transition_at=3;
    assert(nord2_smp_prepare_return()==1 && now==3 && !nord2_psci_probe_active);
    reset(); memset(scratch,0xa5,sizeof(scratch)); nord2_psci_low_alias=scratch;
    state[2]=0;
    assert(nord2_smp_prepare_return()==-1 && nord2_psci_low_alias==scratch);
    for (unsigned i=0;i<sizeof(scratch);i++) assert(scratch[i]==0xa5);
    state[2]=1;
    assert(nord2_smp_prepare_return()==1 && !nord2_psci_low_alias);
    for (unsigned i=0;i<sizeof(scratch);i++) assert(scratch[i]==0);
    struct nord2_psci_mailbox box={0};
    reset(); box.state=1;
    assert(nord2_psci_wait_off(&box,3,5) && !polls);
    /* OFF alone is not a successful entry; no stale/empty mailbox accepted. */
    reset(); box.state=0;
    assert(!nord2_psci_wait_off(&box,3,5) && now==5);
    reset(); box.state=1; state[3]=0;
    assert(!nord2_psci_wait_off(&box,3,5) && now==5);
    reset(); box.state=2; state[3]=0;
    assert(!nord2_psci_wait_off(&box,3,5) && !polls);
    reset(); box.state=1; state[3]=-1;
    assert(!nord2_psci_wait_off(&box,3,5) && !polls);
    reset(); box.state=0; state[3]=2; transition_at=2; mail_state=&box.state;
    assert(nord2_psci_wait_off(&box,3,5) && now==2 && box.state==1);
    puts("PASS: exact PSCI guards; all seven targets; ON/PENDING/errors refuse "
         "return; timeout; late OFF; failed CPU_OFF; empty mailbox; inactive no-op; "
         "low scratch retained until all cores OFF, then fully cleared");
}
