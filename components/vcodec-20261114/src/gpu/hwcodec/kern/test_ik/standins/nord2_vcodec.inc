/* SPDX-License-Identifier: MIT */
/*
 * TEST STAND-IN, NOT A DELIVERABLE: the shape of K02's kern/bsd/dev/nord2_vcodec.inc as far as the 0299 build glue is
 * concerned.  compile_check.sh uses it until the real file exists, to prove that the staged sweet_touch.o builds with
 * the relay and that every relay entry point is referenced (the kernel build is -Werror, so an unused static function
 * in nord2_vcodec_relay.inc would fail).  The real file must call the same functions; see kern/INTEGRATION.md.
 */
static int
n2vc_stub_ioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	if (n2vr_relay_cmd(cmd)) { return n2vr_ioctl(dev, cmd, data, flag, p); }
	return ENOTTY;                          /* the real file implements RWVC_POWER/REGS/DMA/WAIT_IRQ here */
}

static void
n2vc_stub_info(struct rwvc_info *out)
{
	out->powered_mask = 0;                  /* the real file fills powered_mask, pool_*, irq_mode */
	out->pool_pa = 0;
	out->pool_bytes = 0;
	out->pool_free = 0;
	out->irq_mode = 0;
	(void)n2vr_server_present();
}

static void
nord2_vcodec_start(void)
{
	static const struct n2vr_hw hw = { .ioctl = n2vc_stub_ioctl, .info = n2vc_stub_info };
	bool made = n2vr_start(&hw);
	(void)n2vr_dma_allowed(0, NULL, 0, 1, NULL);          /* the real file applies it in its DMA table */
	printf("NORD2 VCODEC: stub /dev/redwood-vcodec-client %s\n", made ? "made" : "CANNOT BE MADE");
}
