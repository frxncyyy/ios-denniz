
#include "../../../metal/tegu/tessellator.h"
int run(unsigned domain, unsigned partition, unsigned winding, float maxf, const float *edge, const float *inside,
        float *vert, unsigned vcap, unsigned *idx, unsigned icap, unsigned *nv, unsigned *ni, unsigned base) {
    TessConfig c = {domain, partition, winding, maxf};
    TessVertex *tv = vcap ? (TessVertex *)__builtin_alloca(sizeof(TessVertex) * vcap) : 0;
    TessOut o = {tv, idx, vcap, icap, 0, 0, base, 0};
    int r = tess_patch(&c, edge, inside, &o);
    for (unsigned i = 0; i < o.vertex_count && i < vcap; i++) { vert[3*i] = tv[i].u; vert[3*i+1] = tv[i].v; vert[3*i+2] = tv[i].w; }
    *nv = o.vertex_count; *ni = o.index_count; return r;
}
int axis(float f, unsigned partition, float maxf, float *s) {
    TessAxis a; int r = tess_axis(f, partition, maxf, &a); if (r) return -1;
    for (int i = 0; i <= a.n; i++) { s[i] = a.s[i]; }
    return a.n;
}
float half_to_float(unsigned short h) { return tess_half_to_float(h); }
void read_factors(unsigned domain, const unsigned char *p, float scale, float *edge, float *inside) { tess_read_factors(domain, p, scale, edge, inside); }
