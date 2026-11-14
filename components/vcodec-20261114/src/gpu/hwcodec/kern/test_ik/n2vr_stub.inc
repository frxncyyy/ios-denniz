/* SPDX-License-Identifier: MIT */
/*
 * Optional host stand-ins for the relay entry points (kern/bsd/dev/nord2_vcodec_relay.inc), for a host build that
 * compiles nord2_vcodec.inc's N2VC_KERNEL paths WITHOUT including the relay file.  Include this AFTER nord2_vcodec.inc;
 * it defines struct n2vr_hw, so do not combine it with the real relay file (which defines the same struct and functions).
 * K02's own host test passes -DN2VC_HOST (N2VC_KERNEL 0), so it does not need this file at all.
 */
#ifndef N2VR_STUB_INC
#define N2VR_STUB_INC 1

struct n2vr_hw {
	int (*ioctl)(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p);
	void (*info)(struct rwvc_info *out);
};

static bool n2vr_start(const struct n2vr_hw *hw) { (void)hw; return true; }
static void n2vr_server_open(void) { }
static void n2vr_server_close(void) { }
static uint32_t n2vr_server_present(void) { return 0; }
static bool n2vr_relay_cmd(u_long cmd) { return cmd == RWVC_MSG_SEND || cmd == RWVC_MSG_RECV; }
static int n2vr_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p) { (void)dev; (void)cmd; (void)data; (void)flag; (void)p; return ENOTTY; }
static bool n2vr_dma_allowed(unsigned owner_minor, struct proc *owner_proc, uint32_t alloc_flags, unsigned accessor_minor, struct proc *accessor_proc)
{
	(void)alloc_flags;
	return owner_minor == accessor_minor && owner_proc == accessor_proc;
}
static void n2vc_relay_closed(struct proc *p) { (void)p; }

#endif /* N2VR_STUB_INC */
