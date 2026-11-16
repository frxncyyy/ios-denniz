/* Formats every TFU1 writer predicate and the fields it checks.  Include
 * after native_package_ui1_writer.c (its static predicates).  Shared by the
 * host probe and the traced bundle's UI1 refusal record. */
#ifndef TEGU_FIXED_UI_UI1_DIAG_H
#define TEGU_FIXED_UI_UI1_DIAG_H
__attribute__((unused)) static int tegu_ui1_diagnose(const struct tegu_mali_graphics *vs, const struct tegu_mali_graphics *fs,
    const struct tegu_mali_graphics_pair2 *pair, const struct tegu_mali_fixed_ui_plan *back,
    const TeguAIRFixedUiR80Plan *front, char *out, size_t out_length)
{
    int n = snprintf(out, out_length,
        "{\"stage_vs\":%d,\"stage_fs\":%d,\"pair\":%d,\"backend\":%d,\"frontend\":%d,"
        "\"vs\":{\"size\":%zu,\"regs\":%u,\"tls\":%u,\"fau\":%u,\"sysval\":%u,\"push\":%u,\"preload\":\"0x%llx\"},"
        "\"fs\":{\"size\":%zu,\"regs\":%u,\"tls\":%u,\"fau\":%u,\"sysval\":%u,\"push\":%u,\"preload\":\"0x%llx\"},"
        "\"pair\":{\"flags\":%u,\"pos\":[%u,%u],\"sec\":[%u,%u],\"sec_regs\":%u,\"generic\":%u,\"sec_preload\":\"0x%llx\","
        "\"attr_mask\":%u,\"vs_var\":%u,\"fs_var\":%u,\"ld_var\":%u,\"noperspective\":%u,\"ext_fifo\":%u,\"varyings\":%u},"
        "\"backend\":{\"version\":%u,\"table\":%u,\"resources\":%u,\"textures\":%u,\"constant\":%u,\"access\":%u,\"ubos\":[",
        stage_ok(vs, 0), stage_ok(fs, 1), pair_ok(pair, vs), backend_ok(back), frontend_ok(front),
        vs->size, vs->registers, vs->tls, vs->fau_words, vs->sysval_mask, vs->push_bytes, (unsigned long long)vs->preload,
        fs->size, fs->registers, fs->tls, fs->fau_words, fs->sysval_mask, fs->push_bytes, (unsigned long long)fs->preload,
        pair->flags, pair->position_offset, pair->position_size, pair->secondary_offset, pair->secondary_size,
        pair->secondary_registers, pair->generic_size_B, (unsigned long long)pair->secondary_preload,
        pair->vs_attribute_mask, pair->vs_varying_mask, pair->fs_varying_mask, pair->fs_uses_ld_var,
        pair->noperspective_mask, pair->needs_extended_fifo, pair->varying_count,
        back->version, back->table, back->resource_count, back->texture_count, back->constant_bytes, back->constant_access_bytes);
    for (unsigned i = 0; i < 3 && n > 0 && (size_t)n < out_length; i++) {
        const struct tegu_mali_resource_ubo *u = &back->ubos[i];
        n += snprintf(out + n, out_length - (size_t)n, "%s[%u,%u,%u,%u,%u,%u]", i ? "," : "",
            u->stage, u->descriptor_set, u->binding, u->table, u->index, u->declared_bytes);
    }
    if (n > 0 && (size_t)n < out_length)
        n += snprintf(out + n, out_length - (size_t)n,
            "]},\"front\":{\"bindings\":%u,\"pushes\":%u,\"push_len\":%u,\"const_stage\":%u,\"const_len\":%u,"
            "\"b0\":[%u,%u,%u,%u,%llu,%llu,%llu,%llu],\"b1\":[%u,%u,%u,%u,%llu,%llu,%llu,%llu],"
            "\"p0\":[%u,%u,%u,%u],\"p1\":[%u,%u,%u,%u]}}",
            front->binding_count, front->push_count, front->push_bytes_length, front->constant_stage, front->constant_length,
            front->bindings[0].stage, front->bindings[0].index, front->bindings[0].descriptor_set, front->bindings[0].binding,
            (unsigned long long)front->bindings[0].bind_offset, (unsigned long long)front->bindings[0].resource_length,
            (unsigned long long)front->bindings[0].descriptor_offset, (unsigned long long)front->bindings[0].descriptor_length,
            front->bindings[1].stage, front->bindings[1].index, front->bindings[1].descriptor_set, front->bindings[1].binding,
            (unsigned long long)front->bindings[1].bind_offset, (unsigned long long)front->bindings[1].resource_length,
            (unsigned long long)front->bindings[1].descriptor_offset, (unsigned long long)front->bindings[1].descriptor_length,
            front->pushes[0].stage, front->pushes[0].offset, front->pushes[0].length, front->pushes[0].bytes_offset,
            front->pushes[1].stage, front->pushes[1].offset, front->pushes[1].length, front->pushes[1].bytes_offset);
    return n;
}
#endif
