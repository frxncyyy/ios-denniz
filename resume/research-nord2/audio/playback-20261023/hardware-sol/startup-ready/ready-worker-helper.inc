static int prepare_native_deferred(struct nah_io *io,void **cookie)
{
 const uint64_t limit=60000000;uint64_t begin=now_us();
 for (;;) {
  int result=nah_native_prepare(io,cookie);
  if (result!=NAH_NATIVE_WAIT) return result;
  hardware.status.error=NAH_NATIVE_WAIT;
  commands();
  uint64_t now=now_us();
  if (now<begin || now-begin>=limit) return NAH_NATIVE_E_TIMEOUT;
  native_wait_pause();
 }
}
