static void diagnose(const char *where,int reason,unsigned initial) {
 /* Saved evidence only: never read hardware again after a failed access.
  * Trailing fields: last I2C8 register/value, inherited ownership summary,
  * per-amp saved status (post-reset SYS_CONTROL0 after probe). */
 struct nah_status saved;nau_hw_status(&hardware,&saved);
 const struct nah_status *h=&saved;
 printf("NORD2 AUDIO ERROR: where=%s reason=%d gate=%x afe=%u/%u i2c=%u/%u pa=%x stop=%d reset=%d held=%u uncertain=%u i2cpa=%x i2cv=%x own=%x st=%04x/%04x\n",
  where,reason,h->gate,h->afe_phase,h->afe_op,h->i2c_stage,h->i2c_failure,
  h->last_pa,h->stop_error,h->reset_error,h->amp_reset_held,h->uncertain,
  h->i2c_last_pa,h->i2c_last_value,h->ownership,h->amp_status[0],h->amp_status[1]);
 /* Worker-owned saved values only; diagnostics never perform MMIO. */
 printf("NORD2 AUDIO AP: valid=%x start=%x ctrl=%x irq=%x fifo=%x i3c=%x\n",
  hardware.bus.ap_valid,hardware.bus.ap_preflight[NA8_AP_START],
  hardware.bus.ap_preflight[NA8_AP_CONTROL],hardware.bus.ap_preflight[NA8_AP_IRQ],
  hardware.bus.ap_preflight[NA8_AP_FIFO],hardware.bus.ap_preflight[NA8_AP_I3C]);
 if(initial)for(unsigned i=0;i<hardware.snapshot.snap_read && i<NAU_SNAP_COUNT;i++)
  printf("NORD2 AUDIO SNAP: index=%u pa=%x value=%x\n",i,nau_snapshot_pa[i],hardware.snapshot.snap[i]);
}
