/* Digest of all outputs of tegu_mali_compile_generic_pair_options. */
static void speed_out_digest(int r, const struct tegu_mali_graphics *vo, const struct tegu_mali_graphics *fo,
                             const struct tegu_mali_graphics_pair2 *pair, const struct tegu_mali_generic_plan *plan,
                             const struct tegu_mali_varying_info *varying, const char *msg, size_t msgn, uint8_t d[32]) {
  struct sha256 h;
  sha256_init(&h);
  sha256_update(&h, &r, 4);
  const struct tegu_mali_graphics *g[2] = {vo, fo};
  for (int i = 0; i < 2; i++) {
    uint64_t sz = g[i]->size;
    sha256_update(&h, &sz, 8);
    if (g[i]->code) sha256_update(&h, g[i]->code, g[i]->size);
    uint32_t f[6] = {g[i]->stage, g[i]->registers, g[i]->tls, g[i]->fau_words, g[i]->sysval_mask, g[i]->push_bytes};
    sha256_update(&h, f, sizeof f);
    sha256_update(&h, &g[i]->preload, 8);
  }
  if (pair) sha256_update(&h, pair, sizeof *pair);
  if (plan) sha256_update(&h, plan, sizeof *plan);
  if (varying) sha256_update(&h, varying, sizeof *varying);
  if (msg && msgn) sha256_update(&h, msg, strnlen(msg, msgn));
  sha256_final(&h, d);
}
