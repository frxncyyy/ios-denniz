/*
 * 0277 NORD2 BB: the interface between the in-kernel MediaTek modem service (bsd/kern/sweet_amfi.c) and the baseband bridge that
 * CommCenter/ABM talks to (iokit/Kernel/arm/Nord2Baseband.cpp). Plain C, included by both.
 *
 * - The bridge installs nord2_bb_hooks from its static constructor (OSlibkernInit, long before the modem thread). Without the bridge
 *   object in the link the pointer stays NULL and the modem service behaves exactly as 0276.
 * - nord2_at_query() is the kernel-internal AT client: any thread may queue a command; the modem service thread sends it on the
 *   shared AT channel (ttyC0, CCCI ch 12/10) when that channel is idle and calls `done` with the collected response.
 */
#ifndef NORD2_BB_KPI_H
#define NORD2_BB_KPI_H

#ifdef __cplusplus
extern "C" {
#endif

#define NORD2_BB_EV_THREAD 1        /* the modem service thread started (t = boot + 10 s): the bridge publishes its services */
#define NORD2_BB_EV_READY  2        /* md_state READY (HS2): AT commands can be sent */
#define NORD2_BB_EV_STEADY 3        /* the boot AT script is done, steady service */
#define NORD2_BB_EV_DEAD   4        /* the MD signalled an exception: no more AT */
#define NORD2_BB_EV_EARLY  5        /* 0277k: bsd_init, before the first exec: the bridge puts its "baseband" node in IODeviceTree */
#define NORD2_BB_EV_PDN    6        /* 0277l: the PDN of cid 1 got, changed or lost its address (nord2_pdn_get) */

struct nord2_bb_hooks {
	/* NORD2_BB_EV_*, called on the modem service thread; must not block (take a lock briefly, schedule a thread call). */
	void (*event)(int what);
	/* One complete AT line that no kernel user consumed (URCs, /dev/n2at answers), NUL terminated, on the modem service thread.
	 * It can carry identities (caller ids, SMS): the bridge only looks at its prefix and never logs it. Must not block.
	 * 0277r: also RING, NO CARRIER/BUSY/NO ANSWER (unless they end an ATD/ATA in flight) and the PDU line that follows a +CMT/+CDS/
	 * +CBM URC, all three even while a kernel or bridge command is in flight. */
	void (*urc)(const char *line);
	/* 0277c tracing of the baseband stack's processes (CommCenter, abm-helper), fed by sweet_amfi.c's Sandbox MAC policy hooks only
	 * for those processes (sweet_amfi.c filters by name first). Any thread context; must not block for long. */
	void (*trace_path)(const char *tag, const char *path, unsigned long len);   /* namei paths ("lookup"), existing ones ("stat"),
	                                                                             * sysctl names ("sysctl", "sysctl-set", 0277e) */
	void (*trace_ioprop)(void *entry, const char *name);                       /* IORegistryEntryCreateCFProperty & co (OSObject *) */
	void (*trace_ioopen)(void *service, unsigned type);                        /* IOServiceOpen (OSObject *) */
	/* 0277e. A Sandbox check (sandbox_check*: __mac_syscall("Sandbox", 2)) whose subject is CommCenter or abm-helper, made by any
	 * process (launchd checks every "mach-lookup" this way): `subject` the checked process, `op` the operation ("mach-lookup", ...),
	 * `arg` the filter argument when it is a name or path, else "". Strings are kernel copies, NUL terminated. Must not block. */
	void (*trace_check)(const char *subject, const char *op, const char *arg);
	/* 0277e. CommCenter's path lookups (as the caller gave them), before namei resolves them: 0 lets the lookup go on, an errno fails
	 * it. Used once per boot, for the stale /tmp/ct.shutdown marker (Nord2Baseband.cpp bb_path_gate). Must not block. */
	int (*path_gate)(const char *path, unsigned long len);
};
extern const struct nord2_bb_hooks *nord2_bb_hooks;

/* `final`: 0 OK, 1 ERROR / +CME ERROR / +CMS ERROR (the final line is the last line of `text`), 2 timeout or modem gone (text may be
 * partial). `text`: the response lines (echo dropped, URCs excluded), each ended by '\n'; valid only during the call; it can hold
 * IMEI/IMSI/ICCID and must never be logged. Runs on the modem service thread with no AT lock held; must not block. */
typedef void (*nord2_at_done_t)(void *ctx, int final, const char *text, unsigned len);

#define NORD2_AT_FINAL_OK      0
#define NORD2_AT_FINAL_ERROR   1
#define NORD2_AT_FINAL_TIMEOUT 2

/* Queue one command ("AT+CSQ", no '\r', at most 61 characters). 0 queued, EINVAL bad command, EBUSY queue full (32), ENXIO the
 * modem is dead. Queued commands wait for md READY; they are failed (final 2) if the modem dies. */
int nord2_at_query(const char *cmd, nord2_at_done_t done, void *ctx);

/* 0277r: AT+CMGS in PDU mode. `cmd` ("AT+CMGS=<TPDU length>") is queued like nord2_at_query; when the modem's "> " prompt comes, the
 * service thread sends `pdu` (hex, SMSC part first, at most NORD2_AT_PDU_MAX characters) and Ctrl-Z. One such command at a time:
 * EBUSY while another one is queued or running. The PDU is never logged; the command may run up to 60 s (the network's answer). */
#define NORD2_AT_PDU_MAX 512
int nord2_at_query_pdu(const char *cmd, const char *pdu, nord2_at_done_t done, void *ctx);

#define NORD2_MODEM_READY  1u       /* HS2 seen */
#define NORD2_MODEM_STEADY 2u       /* boot script done */
#define NORD2_MODEM_DEAD   4u       /* exception signalled */
unsigned nord2_modem_state(void);

/*
 * 0277l: cellular data owned by CommCenter. sweet_amfi.c brings up the PDN of cid 1 itself (AT+CGDCONT/CGACT on its APN, +EIF,
 * DPMAIF); with nord2_pdn_owner set (by the bridge, at NORD2_BB_EV_EARLY) it no longer configures an address or routes: it creates the
 * five interfaces of iOS's PDP manager, pdp_ip0..pdp_ip4 (nord2_pdn_ifaces, from the bridge's publication thread call), hands the PDN's
 * downlink to the one CommCenter's WDS call is bound to (nord2_pdn_route: pdp = WDS Bind Mux Data Port mux id - 1) and takes uplink
 * from any of them; configd (IPConfiguration, IPMonitor) configures the address, routes and DNS that CommCenter got from WDS.
 */
extern int nord2_pdn_owner;
struct nord2_pdn_info {
	uint32_t up;                 /* the PDN has an address */
	uint32_t ip, dns[2], mtu;    /* host order */
	uint32_t ch;                 /* DPMAIF channel (ccmni index) */
	const char *apn;             /* the APN sweet_amfi.c activates */
};
int nord2_pdn_get(struct nord2_pdn_info *out);   /* 0 when out->up */
void nord2_pdn_route(int pdp);                   /* downlink to pdp_ip<pdp>, -1: dropped (counted) */
int nord2_pdn_ifaces(void);                      /* create pdp_ip0..4 now (any thread that may attach an ifnet); 0 ok */

#ifdef __cplusplus
}
#endif

#endif /* NORD2_BB_KPI_H */
