static void diagnose(const char *where,int reason,unsigned initial) {
 /* Saved evidence only: never read hardware again after a failed access. */
 struct nah_status saved;nau_hw_status(&hardware,&saved);
 const struct nah_status *h=&saved;
 printf("NORD2 AUDIO ERROR: where=%s reason=%d gate=%x afe=%u/%u i2c=%u/%u pa=%x stop=%d reset=%d held=%u uncertain=%u\n",
  where,reason,h->gate,h->afe_phase,h->afe_op,h->i2c_stage,h->i2c_failure,
  h->last_pa,h->stop_error,h->reset_error,h->amp_reset_held,h->uncertain);
 if(initial)for(unsigned i=0;i<hardware.snapshot.snap_read && i<NAU_SNAP_COUNT;i++)
  printf("NORD2 AUDIO SNAP: index=%u pa=%x value=%x\n",i,nau_snapshot_pa[i],hardware.snapshot.snap[i]);
}
