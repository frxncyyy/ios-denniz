
#include "disassemble.h"

#define BIT(b)         (1ull << (b))
#define MASK(count)    ((1ull << (count)) - 1)
#define SEXT(b, count) ((b ^ BIT(count - 1)) - BIT(count - 1))
#define UNUSED         __attribute__((unused))

#define VA_SRC_UNIFORM_TYPE 0x2
#define VA_SRC_IMM_TYPE     0x3

UNUSED static const char *valhall_flow[] = {
    "",
    ".wait0",
    ".wait1",
    ".wait01",
    ".wait2",
    ".wait02",
    ".wait12",
    ".wait012",
    ".wait0126",
    ".wait",
    ".reconverge",
    ".reserved",
    ".reserved",
    ".discard",
    ".reserved",
    ".end",
};

UNUSED static const char *valhall_fau_special_page_0[] = {
    ".reserved",
    ".reserved",
    ".warp_id",
    ".reserved",
    ".framebuffer_size",
    ".atest_datum",
    ".sample",
    ".reserved",
    ".blend_descriptor_0",
    ".blend_descriptor_1",
    ".blend_descriptor_2",
    ".blend_descriptor_3",
    ".blend_descriptor_4",
    ".blend_descriptor_5",
    ".blend_descriptor_6",
    ".blend_descriptor_7",
};

UNUSED static const char *valhall_fau_special_page_1[] = {
    ".reserved",
    ".thread_local_pointer",
    ".reserved",
    ".workgroup_local_pointer",
    ".reserved",
    ".reserved",
    ".reserved",
    ".resource_table_pointer",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_fau_special_page_3[] = {
    ".reserved",
    ".lane_id",
    ".reserved",
    ".core_id",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".program_counter",
};

UNUSED static const char *valhall_swizzles_8_bit[] = {
    "",
    ".b3210",
    ".b0101",
    ".b2323",
    ".b0000",
    ".b1111",
    ".b2222",
    ".b3333",
    ".b2301",
    ".b1032",
    ".b0011",
    ".b2233",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_lanes_8_bit[] = {
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".b00",
    ".b11",
    ".b22",
    ".b33",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_half_swizzles_8_bit[] = {
    ".b00",
    ".b10",
    ".b20",
    ".b30",
    ".b01",
    ".b11",
    ".b21",
    ".b31",
    ".b02",
    ".b12",
    ".b22",
    ".b32",
    ".b03",
    ".b13",
    ".b23",
    ".b33",
};

UNUSED static const char *valhall_swizzles_16_bit[] = {
    ".h00",
    ".h10",
    "",
    ".h11",
    ".b00",
    ".b20",
    ".b02",
    ".b22",
    ".b11",
    ".b31",
    ".b13",
    ".b33",
    ".b01",
    ".b23",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_swizzles_32_bit[] = {
    "",
    ".reserved",
    ".h0",
    ".h1",
    ".b0",
    ".b1",
    ".b2",
    ".b3",
};

UNUSED static const char *valhall_swizzles_64_bit[] = {
    "",
    ".reserved",
    ".h0",
    ".h1",
    ".b0",
    ".b1",
    ".b2",
    ".b3",
    ".w0",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_lane_8_bit[] = {
    ".b0",
    ".b1",
    ".b2",
    ".b3",
};

UNUSED static const char *valhall_combine[] = {
    "",
    ".h0",
    ".h1",
    ".and",
    ".lowbits",
};

UNUSED static const char *valhall_lane_16_bit[] = {
    ".h0",
    ".h1",
};

UNUSED static const char *valhall_load_lane_8_bit[] = {
    "",
    ".b1",
    ".b2",
    ".b3",
    ".h0",
    ".h1",
    ".w0",
    ".d0",
};

UNUSED static const char *valhall_load_lane_16_bit[] = {
    "",
    ".h1",
    ".w0",
    ".d0",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_load_lane_24_bit[] = {
    "",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_load_lane_32_bit[] = {
    "",
    ".d0",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_load_lane_48_bit[] = {
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    "",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_load_lane_64_bit[] = {
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    "",
};

UNUSED static const char *valhall_load_lane_96_bit[] = {
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    "",
    ".reserved",
};

UNUSED static const char *valhall_load_lane_128_bit[] = {
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    "",
};

UNUSED static const char *valhall_round_mode[] = {
    "",
    ".rtp",
    ".rtn",
    ".rtz",
};

UNUSED static const char *valhall_result_type[] = {
    ".i1",
    ".f1",
    ".m1",
    ".u1",
};

UNUSED static const char *valhall_widen[] = {
    "",
    ".h0",
    ".h1",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
};

UNUSED static const char *valhall_clamp[] = {
    "",
    ".clamp_0_inf",
    ".clamp_m1_1",
    ".clamp_0_1",
};

UNUSED static const char *valhall_condition[] = {
    ".eq",
    ".gt",
    ".ge",
    ".ne",
    ".lt",
    ".le",
    ".gtlt",
    ".total",
};

UNUSED static const char *valhall_dimension[] = {
    ".1d",
    ".2d",
    ".3d",
    ".cube",
};

UNUSED static const char *valhall_lod_mode[] = {
    ".zero",
    ".computed",
    ".reserved",
    ".reserved",
    ".explicit",
    ".computed_bias",
    ".grdesc",
    ".reserved",
};

UNUSED static const char *valhall_register_format[] = {
    ".auto",
    ".reserved",
    ".f32",
    ".f16",
    ".s32",
    ".s16",
    ".u32",
    ".u16",
};

UNUSED static const char *valhall_staging_register_count[] = {
    ".sr0",
    ".sr1",
    ".sr2",
    ".sr3",
    ".sr4",
    ".sr5",
    ".sr6",
    ".sr7",
};

UNUSED static const char *valhall_staging_register_write_count[] = {
    ".write1",
    ".write2",
    ".write3",
    ".write4",
    ".write5",
    ".write6",
    ".write7",
    ".write8",
};

UNUSED static const char *valhall_write_mask[] = {
    ".reserved",
    ".r",
    ".g",
    ".rg",
    ".b",
    ".rb",
    ".gb",
    ".rgb",
    ".a",
    ".ra",
    ".ga",
    ".rga",
    ".ba",
    ".rba",
    ".gba",
    "",
};

UNUSED static const char *valhall_fetch_component[] = {
    ".gather4_r",
    ".gather4_g",
    ".gather4_b",
    ".gather4_a",
};

UNUSED static const char *valhall_register_type[] = {
    ".reserved",
    ".f",
    ".u",
    ".s",
};

UNUSED static const char *valhall_register_width[] = {
    ".16",
    ".32",
};

UNUSED static const char *valhall_varying_texture_register_width[] = {
    ".16",
    ".32",
    ".16.32",
    ".32.32",
};

UNUSED static const char *valhall_vector_size[] = {
    "",
    ".v2",
    ".v3",
    ".v4",
};

UNUSED static const char *valhall_slot[] = {
    ".slot0",
    ".slot1",
    ".slot2",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".slot7",
};

UNUSED static const char *valhall_memory_access[] = {
    "",
    ".istream",
    ".estream",
    ".force",
};

UNUSED static const char *valhall_subgroup_size[] = {
    ".subgroup2",
    ".subgroup4",
    ".subgroup8",
    "",
};

UNUSED static const char *valhall_lane_operation[] = {
    "",
    ".xor",
    ".accumulate",
    ".shift",
};

UNUSED static const char *valhall_inactive_result[] = {
    "",
    ".umax",
    ".i1",
    ".v2i1",
    ".smin",
    ".smax",
    ".v2smin",
    ".v2smax",
    ".v4smin",
    ".v4smax",
    ".f1",
    ".v2f1",
    ".infn",
    ".inf",
    ".v2infn",
    ".v2inf",
};

UNUSED static const char *valhall_mux[] = {
    ".neg",
    "",
    ".fp_zero",
    ".bit",
};

UNUSED static const char *valhall_sample_mode[] = {
    ".center",
    ".centroid",
    ".sample",
    ".explicit",
};

UNUSED static const char *valhall_update_mode[] = {
    ".store",
    ".retrieve",
    ".reserved",
    ".clobber",
};

UNUSED static const char *valhall_sample_and_update_mode[] = {
    ".center_store",
    ".centroid_store",
    ".sample_store",
    ".explicit_store",
    ".center_clobber",
    ".reserved",
    ".sample_clobber",
    ".retrieve",
};

UNUSED static const char *valhall_source_format[] = {
    ".src_flat32",
    ".src_flat16",
    ".src_f32",
    ".src_f16",
};

UNUSED static const char *valhall_atomic_operation[] = {
    ".reserved",
    ".reserved",
    ".aadd",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".reserved",
    ".asmin",
    ".asmax",
    ".aumin",
    ".aumax",
    ".aand",
    ".aor",
    ".axor",
    ".axchg",
};

UNUSED static const char *valhall_atomic_operation_with_1[] = {
    ".ainc",
    ".adec",
    ".aumax1",
    ".asmax1",
    ".aor1",
};

UNUSED static const char *valhall_nan_mode[] = {
    ".none",
    ".flush_nan",
    ".quiet_nan",
};

static const uint32_t va_immediates[32] = {
    0x0,
    0xffffffff,
    0x7fffffff,
    0xfafcfdfe,
    0x1000000,
    0x80002000,
    0x70605030,
    0xc0b0a090,
    0x3020100,
    0x7060504,
    0xb0a0908,
    0xf0e0d0c,
    0x13121110,
    0x17161514,
    0x1b1a1918,
    0x1f1e1d1c,
    0x3f800000,
    0x3dcccccd,
    0x3ea2f983,
    0x3f317218,
    0x40490fdb,
    0x0,
    0x477fff00,
    0x5c005bf8,
    0x2e660000,
    0x34000000,
    0x38000000,
    0x3c000000,
    0x40000000,
    0x44000000,
    0x48000000,
    0x42480000,
};

static inline void
va_print_src(FILE *fp, uint8_t src, unsigned fau_page)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        if (value >= 32) {
            if (fau_page == 0)
                fputs(valhall_fau_special_page_0[(value - 0x20) >> 1] + 1, fp);
            else if (fau_page == 1)
                fputs(valhall_fau_special_page_1[(value - 0x20) >> 1] + 1, fp);
            else if (fau_page == 3)
                fputs(valhall_fau_special_page_3[(value - 0x20) >> 1] + 1, fp);
            else
                fprintf(fp, "reserved_page2");

            fprintf(fp, ".w%u", value & 1);
        } else {
            fprintf(fp, "0x%X", va_immediates[value]);
        }
	} else if (type == VA_SRC_UNIFORM_TYPE) {
		fprintf(fp, "u%u", value | (fau_page << 6));
	} else {
		bool discard = (type & 1);
		fprintf(fp, "%sr%u", discard ? "^" : "", value);
	}
}

static inline void
va_print_float_src(FILE *fp, uint8_t src, unsigned fau_page, bool neg, bool abs)
{
	unsigned type = (src >> 6);
	unsigned value = (src & 0x3F);

	if (type == VA_SRC_IMM_TYPE) {
        assert(value < 32 && "overflow in LUT");
        fprintf(fp, "0x%X", va_immediates[value]);
	} else {
        va_print_src(fp, src, fau_page);
    }

	if (neg)
		fprintf(fp, ".neg");

	if (abs)
		fprintf(fp, ".abs");
}

static inline void
va_print_dest(FILE *fp, uint8_t dest, bool can_mask)
{
   unsigned mask = (dest >> 6);
   unsigned value = (dest & 0x3F);
   fprintf(fp, "r%u", value);

   /* Should write at least one component */
   //	assert(mask != 0);
   //	assert(mask == 0x3 || can_mask);

   if (mask != 0x3)
      fprintf(fp, ".h%u", (mask == 1) ? 0 : 1);
}

void
va_disasm_instr(FILE *fp, uint64_t instr)
{
   unsigned primary_opc = (instr >> 48) & MASK(9);
   unsigned fau_page = (instr >> 57) & MASK(2);
   unsigned secondary_opc = 0;

   switch (primary_opc) {
    
   case 0x0:

            fputs("NOP", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);
     break;

    
   case 0x1f:

            fputs("BRANCHZ", fp);
            if (instr & BIT(35)) fputs(".conservative", fp);
            if (instr & BIT(36)) fputs(".eq", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_combine[(instr >> 37) & 0x7], fp);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(27)) , 27));
     break;

    
   case 0x20:

            fputs("DISCARD.f32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
     break;

    
   case 0x2f:

            fputs("BRANCHZI", fp);
            if (instr & BIT(35)) fputs(".conservative", fp);
            if (instr & BIT(36)) fputs(".eq", fp);
            if (instr & BIT(40)) fputs(".absolute", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_combine[(instr >> 37) & 0x7], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
     break;

    
   case 0x45:

            fputs("BARRIER", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);
     break;

    
   case 0x154:

            fputs("CSEL.f32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 24, fau_page, false, false);
     break;

    
   case 0x155:

            fputs("CSEL.v2f16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page, false, false);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 24, fau_page, false, false);
     break;

    
   case 0x150:

            fputs("CSEL.u32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x151:

            fputs("CSEL.v2u16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x158:

            fputs("CSEL.s32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x159:

            fputs("CSEL.v2s16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x56:

            fputs("LD_VAR_SPECIAL", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 12) & MASK(4)) );
     break;

    
   case 0x5c:

            fputs("LD_VAR_BUF_IMM.f32", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_source_format[(instr >> 24) & 0xf], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(8)) );
     break;

    
   case 0x5d:

            fputs("LD_VAR_BUF_IMM.f16", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_source_format[(instr >> 24) & 0xf], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(8)) );
     break;

    
   case 0x6c:

            fputs("LD_VAR_BUF.f32", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_source_format[(instr >> 24) & 0xf], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
     break;

    
   case 0x6d:

            fputs("LD_VAR_BUF.f16", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_source_format[(instr >> 24) & 0xf], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
     break;

    
   case 0x64:

            fputs("LD_VAR", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
     break;

    
   case 0x54:

            fputs("LD_VAR_IMM", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_sample_mode[(instr >> 38) & 0x3], fp);
            fputs(valhall_update_mode[(instr >> 36) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(4)) );

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 12) & MASK(8)) );
     break;

    
   case 0x55:

            fputs("LD_VAR_FLAT", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x41:

            fputs("LD_VAR_FLAT_IMM", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(4)) );

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 12) & MASK(8)) );
     break;

    
   case 0x66:
	secondary_opc = (instr >> 37) & 0x3;


        if (secondary_opc == 0) { 
            fputs("LD_ATTR_IMM", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(4)) );

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(4)) );
        }


        if (secondary_opc == 1) { 
            fputs("LD_TEX_IMM", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(4)) );

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(4)) );
        }
     break;

    
   case 0x76:
	secondary_opc = (instr >> 37) & 0x3;


        if (secondary_opc == 0) { 
            fputs("LD_ATTR", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("LD_TEX", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0x67:
	secondary_opc = (instr >> 37) & 0x3;


        if (secondary_opc == 0) { 
            fputs("LEA_ATTR_IMM", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(4)) );

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(4)) );
        }


        if (secondary_opc == 1) { 
            fputs("LEA_TEX_IMM", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 16) & MASK(4)) );

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(4)) );
        }
     break;

    
   case 0x77:
	secondary_opc = (instr >> 37) & 0x3;


        if (secondary_opc == 0) { 
            fputs("LEA_ATTR", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("LEA_TEX", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0x6a:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 0) { 
            fputs("LD_BUFFER.i8", fp);
            fputs(valhall_load_lane_8_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("LD_BUFFER.i16", fp);
            fputs(valhall_load_lane_16_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 2) { 
            fputs("LD_BUFFER.i24", fp);
            fputs(valhall_load_lane_24_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 3) { 
            fputs("LD_BUFFER.i32", fp);
            fputs(valhall_load_lane_32_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 4) { 
            fputs("LD_BUFFER.i48", fp);
            fputs(valhall_load_lane_48_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 5) { 
            fputs("LD_BUFFER.i64", fp);
            fputs(valhall_load_lane_64_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 6) { 
            fputs("LD_BUFFER.i96", fp);
            fputs(valhall_load_lane_96_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 7) { 
            fputs("LD_BUFFER.i128", fp);
            fputs(valhall_load_lane_128_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }
     break;

    
   case 0x5e:

            fputs("LEA_BUF_IMM", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", table:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(4)) );

            fprintf(fp, ", index:0x%X", (uint32_t) 
                    ((instr >> 12) & MASK(8)) );
     break;

    
   case 0x60:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 0) { 
            fputs("LOAD.i8", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_8_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 1) { 
            fputs("LOAD.i16", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_16_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 2) { 
            fputs("LOAD.i24", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_24_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 3) { 
            fputs("LOAD.i32", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_32_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 4) { 
            fputs("LOAD.i48", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_48_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 5) { 
            fputs("LOAD.i64", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_64_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 6) { 
            fputs("LOAD.i96", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_96_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 7) { 
            fputs("LOAD.i128", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_load_lane_128_bit[(instr >> 36) & 0x7], fp);
            if (instr & BIT(39)) fputs(".unsigned", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }
     break;

    
   case 0x61:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 0) { 
            fputs("STORE.i8", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 1) { 
            fputs("STORE.i16", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 2) { 
            fputs("STORE.i24", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 3) { 
            fputs("STORE.i32", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 4) { 
            fputs("STORE.i48", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 5) { 
            fputs("STORE.i64", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 6) { 
            fputs("STORE.i96", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }


        if (secondary_opc == 7) { 
            fputs("STORE.i128", fp);
            fputs(valhall_memory_access[(instr >> 24) & 0x3], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:%d", (uint32_t) SEXT(
                    ((instr >> 8) & MASK(16)) , 16));
        }
     break;

    
   case 0x6b:

            fputs("LEA_BUFFER", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
     break;

    
   case 0x71:

            fputs("ST_CVT", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_memory_access[(instr >> 37) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
     break;

    
   case 0x78:

            fputs("LD_TILE", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x79:

            fputs("ST_TILE", fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x7f:

            fputs("BLEND", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_vector_size[(instr >> 28) & 0x3], fp);
            fputs(valhall_register_format[(instr >> 24) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);

            fprintf(fp, ", target:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
     break;

    
   case 0x7d:

            fputs("ATEST", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_widen[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x7e:

            fputs("ZS_EMIT", fp);
            if (instr & BIT(25)) fputs(".z", fp);
            if (instr & BIT(24)) fputs(".stencil", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x90:
	secondary_opc = (instr >> 16) & 0x1f;


        if (secondary_opc == 7) { 
            fputs("V2S16_TO_V2F16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 9) { 
            fputs("S32_TO_F32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 23) { 
            fputs("V2U16_TO_V2F16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 25) { 
            fputs("U32_TO_F32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 4) { 
            fputs("S16_TO_S32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 5) { 
            fputs("S16_TO_F32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 20) { 
            fputs("U16_TO_U32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 21) { 
            fputs("U16_TO_F32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 12) { 
            fputs("F32_TO_S32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
        }


        if (secondary_opc == 28) { 
            fputs("F32_TO_U32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
        }


        if (secondary_opc == 14) { 
            fputs("V2F16_TO_V2S16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 30) { 
            fputs("V2F16_TO_V2U16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 10) { 
            fputs("F16_TO_S32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 26) { 
            fputs("F16_TO_U32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 11) { 
            fputs("F16_TO_F32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_lane_16_bit[(instr >> 28) & 0x3], fp);
        }


        if (secondary_opc == 0) { 
            fputs("S8_TO_S32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 28) & 0x3], fp);
        }


        if (secondary_opc == 1) { 
            fputs("S8_TO_F32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 28) & 0x3], fp);
        }


        if (secondary_opc == 16) { 
            fputs("U8_TO_U32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 28) & 0x3], fp);
        }


        if (secondary_opc == 17) { 
            fputs("U8_TO_F32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 28) & 0x3], fp);
        }


        if (secondary_opc == 2) { 
            fputs("V2S8_TO_V2S16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_half_swizzles_8_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 3) { 
            fputs("V2S8_TO_V2F16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_half_swizzles_8_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 18) { 
            fputs("V2U8_TO_V2U16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_half_swizzles_8_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 19) { 
            fputs("V2U8_TO_V2F16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_half_swizzles_8_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 13) { 
            fputs("FROUND.f32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 15) { 
            fputs("FROUND.v2f16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }
     break;

    
   case 0x91:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("MOV.i32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 4) { 
            fputs("CLZ.u32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 5) { 
            fputs("CLZ.v2u16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 6) { 
            fputs("CLZ.v4u8", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 8) { 
            fputs("IABS.s32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 9) { 
            fputs("IABS.v2s16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IABS.v4s8", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 12) { 
            fputs("POPCOUNT.i32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 13) { 
            fputs("BITREV.i32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 14) { 
            fputs("NOT_OLD.i32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }
     break;

    
   case 0x191:

            fputs("NOT_OLD.i64", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x95:

            fputs("WMASK", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x98:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("FLUSH.f32", fp);
            fputs(valhall_nan_mode[(instr >> 8) & 0x3], fp);
            if (instr & BIT(10)) fputs(".ftz", fp);
            if (instr & BIT(11)) fputs(".flush_inf", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 1) { 
            fputs("FLUSH.v2f16", fp);
            fputs(valhall_nan_mode[(instr >> 8) & 0x3], fp);
            if (instr & BIT(10)) fputs(".ftz", fp);
            if (instr & BIT(11)) fputs(".flush_inf", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }
     break;

    
   case 0x99:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("FREXPM.f32", fp);
            if (instr & BIT(24)) fputs(".sqrt", fp);
            if (instr & BIT(25)) fputs(".log", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 1) { 
            fputs("FREXPM.v2f16", fp);
            if (instr & BIT(24)) fputs(".sqrt", fp);
            if (instr & BIT(25)) fputs(".log", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 2) { 
            fputs("FREXPE.f32", fp);
            if (instr & BIT(24)) fputs(".sqrt", fp);
            if (instr & BIT(25)) fputs(".log", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 3) { 
            fputs("FREXPE.v2f16", fp);
            if (instr & BIT(24)) fputs(".sqrt", fp);
            if (instr & BIT(25)) fputs(".log", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }
     break;

    
   case 0x9c:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("FRCP.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 1) { 
            fputs("FRCP.f16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 2) { 
            fputs("FRSQ.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 3) { 
            fputs("FRSQ.f16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 8) { 
            fputs("FLOGD.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 10) { 
            fputs("FPCLASS.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 11) { 
            fputs("FPCLASS.f16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 12) { 
            fputs("FLOG_TABLE.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 14) { 
            fputs("FRCP_APPROX.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 15) { 
            fputs("FRSQ_APPROX.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
        }


        if (secondary_opc == 4) { 
            fputs("FSIN_TABLE.u6", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 5) { 
            fputs("FCOS_TABLE.u6", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 6) { 
            fputs("FSINCOS_OFFSET.u6", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }


        if (secondary_opc == 13) { 
            fputs("FEXP_TABLE.u4", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
        }
     break;

    
   case 0xa4:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("FADD.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 2) { 
            fputs("FMIN.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 3) { 
            fputs("FMAX.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 6) { 
            fputs("LDEXP.f32", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }


        if (secondary_opc == 8) { 
            fputs("FEXP.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
        }


        if (secondary_opc == 9) { 
            fputs("FADD_LSCALE.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
        }


        if (secondary_opc == 14) { 
            fputs("FATAN_ASSIST.f32", fp);
            if (instr & BIT(24)) fputs(".second", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }
     break;

    
   case 0xa5:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("FADD.v2f16", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 2) { 
            fputs("FMIN.v2f16", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 3) { 
            fputs("FMAX.v2f16", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 4) { 
            fputs("V2F32_TO_V2F16", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
        }


        if (secondary_opc == 6) { 
            fputs("LDEXP.v2f16", fp);
            fputs(valhall_round_mode[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
        }
     break;

    
   case 0xa0:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.u32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.u32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.i32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.u32", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.i32", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xa1:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.v2u16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 5) { 
            fputs("MKVEC.v2i16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.v2u16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.v2i16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.v2u16", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.v2u16", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xa2:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.v4u8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.v4u8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.v4i8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.v4u8", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.v4u8", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xa8:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.s32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.s32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.s32", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.s32", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.s32", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xa9:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.v2s16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.v2s16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.v2s16", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.v2s16", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.v2s16", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xaa:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.v4s8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.v4s8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 10) { 
            fputs("IMUL.v4s8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 11) { 
            fputs("HADD.v4s8", fp);
            if (instr & BIT(30)) fputs(".rhadd", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.v4s8", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0x1a3:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.u64", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.u64", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 6) { 
            fputs("SEG_ADD.u64", fp);
            if (instr & BIT(38)) fputs(".neg", fp);
            if (instr & BIT(39)) fputs(".preserve_null", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 7) { 
            fputs("SHADDX.u64", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);

            fprintf(fp, ", shift:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(3)) );
        }


        if (secondary_opc == 10) { 
            fputs("IMULD.u64", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.u64", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0x1ab:
	secondary_opc = (instr >> 16) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IADD.s64", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 1) { 
            fputs("ISUB.s64", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);
        }


        if (secondary_opc == 7) { 
            fputs("SHADDX.s64", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 26) & 0xF], fp);

            fprintf(fp, ", shift:0x%X", (uint32_t) 
                    ((instr >> 20) & MASK(3)) );
        }


        if (secondary_opc == 15) { 
            fputs("CLPER.s64", fp);
            fputs(valhall_subgroup_size[(instr >> 36) & 0xf], fp);
            fputs(valhall_lane_operation[(instr >> 32) & 0x3], fp);
            fputs(valhall_inactive_result[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
        }
     break;

    
   case 0xb2:

            fputs("FMA.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(valhall_widen[(instr >> 24) & 3], fp);
     break;

    
   case 0xb3:

            fputs("FMA.v2f16", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(valhall_swizzles_16_bit[(instr >> 24) & 3], fp);
     break;

    
   case 0xb4:
	secondary_opc = (instr >> 24) & 0x103;


        if (secondary_opc == 256) { 
            fputs("LSHIFT_AND.i32", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 0) { 
            fputs("RSHIFT_AND.i32", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 257) { 
            fputs("LSHIFT_OR.i32", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 1) { 
            fputs("RSHIFT_OR.i32", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 258) { 
            fputs("LSHIFT_XOR.i32", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 2) { 
            fputs("RSHIFT_XOR.i32", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }
     break;

    
   case 0xb5:
	secondary_opc = (instr >> 24) & 0x103;


        if (secondary_opc == 256) { 
            fputs("LSHIFT_AND.v2i16", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 0) { 
            fputs("RSHIFT_AND.v2i16", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 257) { 
            fputs("LSHIFT_OR.v2i16", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 1) { 
            fputs("RSHIFT_OR.v2i16", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 258) { 
            fputs("LSHIFT_XOR.v2i16", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 2) { 
            fputs("RSHIFT_XOR.v2i16", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }
     break;

    
   case 0xb6:
	secondary_opc = (instr >> 24) & 0x103;


        if (secondary_opc == 256) { 
            fputs("LSHIFT_AND.v4i8", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 0) { 
            fputs("RSHIFT_AND.v4i8", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 257) { 
            fputs("LSHIFT_OR.v4i8", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 1) { 
            fputs("RSHIFT_OR.v4i8", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 258) { 
            fputs("LSHIFT_XOR.v4i8", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 2) { 
            fputs("RSHIFT_XOR.v4i8", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }
     break;

    
   case 0x1b7:
	secondary_opc = (instr >> 24) & 0x103;


        if (secondary_opc == 256) { 
            fputs("LSHIFT_AND.i64", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 0) { 
            fputs("RSHIFT_AND.i64", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 257) { 
            fputs("LSHIFT_OR.i64", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 1) { 
            fputs("RSHIFT_OR.i64", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 258) { 
            fputs("LSHIFT_XOR.i64", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }


        if (secondary_opc == 2) { 
            fputs("RSHIFT_XOR.i64", fp);
            if (instr & BIT(34)) fputs(".signed", fp);
            if (instr & BIT(30)) fputs(".not_result", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_64_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lanes_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            if (instr & BIT(35)) fputs(".not", fp);
        }
     break;

    
   case 0xb8:

            fputs("MUX.i32", fp);
            fputs(valhall_mux[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0xb9:

            fputs("MUX.v2i16", fp);
            fputs(valhall_mux[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
            fputs(valhall_swizzles_16_bit[(instr >> 24) & 3], fp);
     break;

    
   case 0xba:

            fputs("MUX.v4i8", fp);
            fputs(valhall_mux[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0xbc:
	secondary_opc = (instr >> 24) & 0xf;


        if (secondary_opc == 0) { 
            fputs("CUBE_SSEL", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("CUBE_TSEL", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xbd:

            fputs("MKVEC.v2i8", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 38) & 0x3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(valhall_lane_8_bit[(instr >> 36) & 0x3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0xc0:

            fputs("CUBEFACE1", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
     break;

    
   case 0xc1:

            fputs("CUBEFACE2_V9", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
     break;

    
   case 0xc2:
	secondary_opc = (instr >> 24) & 0xf;


        if (secondary_opc == 0) { 
            fputs("IDP.v4s8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("IDP.v4u8", fp);
            if (instr & BIT(30)) fputs(".saturate", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf0:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.u32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.u32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 2) { 
            fputs("ICMP_MULTI.u32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf1:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.v2u16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.v2u16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf2:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.v4u8", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.v4u8", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf4:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("FCMP_OR.f32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("FCMP_AND.f32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_widen[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_widen[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf5:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("FCMP_OR.v2f16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("FCMP_AND.v2f16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(valhall_swizzles_16_bit[(instr >> 28) & 3], fp);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(valhall_swizzles_16_bit[(instr >> 26) & 3], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf8:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.s32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.s32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 2) { 
            fputs("ICMP_MULTI.s32", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_32_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xf9:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.v2s16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.v2s16", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_16_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0xfa:
	secondary_opc = (instr >> 24) & 0x3;


        if (secondary_opc == 0) { 
            fputs("ICMP_OR.v4s8", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }


        if (secondary_opc == 1) { 
            fputs("ICMP_AND.v4s8", fp);
            fputs(valhall_condition[(instr >> 32) & 0x7], fp);
            fputs(valhall_result_type[(instr >> 30) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 36) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 8, fau_page);
		    fputs(valhall_swizzles_8_bit[(instr >> 26) & 0xF], fp);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
        }
     break;

    
   case 0x110:

            fputs("IADD_IMM.i32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", #0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(32)) );
     break;

    
   case 0x111:

            fputs("IADD_IMM.v2i16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", #0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(32)) );
     break;

    
   case 0x112:

            fputs("IADD_IMM.v4i8", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", #0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(32)) );
     break;

    
   case 0x114:

            fputs("FADD_IMM.f32", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", #0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(32)) );
     break;

    
   case 0x115:

            fputs("FADD_IMM.v2f16", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page, false, false);

            fprintf(fp, ", #0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(32)) );
     break;

    
   case 0x69:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 3) { 
            fputs("ATOM1_RETURN.i32", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_atomic_operation_with_1[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }


        if (secondary_opc == 5) { 
            fputs("ATOM1_RETURN.i64", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_atomic_operation_with_1[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }
     break;

    
   case 0x68:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 3) { 
            fputs("ATOM.i32", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_atomic_operation[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }


        if (secondary_opc == 5) { 
            fputs("ATOM.i64", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            fputs(valhall_atomic_operation[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 64);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }
     break;

    
   case 0x120:
	secondary_opc = (instr >> 27) & 0x7;


        if (secondary_opc == 3) { 
            fputs("ATOM_RETURN.i32", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(26)) fputs(".compare", fp);
            fputs(valhall_atomic_operation[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 192);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }


        if (secondary_opc == 5) { 
            fputs("ATOM_RETURN.i64", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(26)) fputs(".compare", fp);
            fputs(valhall_atomic_operation[(instr >> 22) & 0xf], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 192);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);

            fprintf(fp, ", offset:0x%X", (uint32_t) 
                    ((instr >> 8) & MASK(8)) );
        }
     break;

    
   case 0x125:

            fputs("TEX_FETCH", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_register_width[(instr >> 46) & 0x1], fp);
            fputs(valhall_write_mask[(instr >> 22) & 0xf], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(8)) fputs(".wide_indices", fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(11)) fputs(".texel_offset", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x128:

            fputs("TEX_SINGLE", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_register_width[(instr >> 46) & 0x1], fp);
            fputs(valhall_write_mask[(instr >> 22) & 0xf], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(8)) fputs(".wide_indices", fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(11)) fputs(".texel_offset", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x129:

            fputs("TEX_GATHER", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_register_width[(instr >> 46) & 0x1], fp);
            fputs(valhall_write_mask[(instr >> 22) & 0xf], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(8)) fputs(".wide_indices", fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(11)) fputs(".texel_offset", fp);
            if (instr & BIT(13)) fputs(".integer_coordinates", fp);
            fputs(valhall_fetch_component[(instr >> 14) & 0x3], fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x12a:

            fputs("TEX_GRADIENT", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_register_width[(instr >> 46) & 0x1], fp);
            fputs(valhall_write_mask[(instr >> 22) & 0xf], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(8)) fputs(".wide_indices", fp);
            if (instr & BIT(13)) fputs(".lod_mode", fp);
            if (instr & BIT(14)) fputs(".lod_mode", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x12f:

            fputs("TEX_DUAL", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_register_width[(instr >> 46) & 0x1], fp);
            fputs(valhall_register_width[(instr >> 47) & 0x1], fp);
            fputs(valhall_write_mask[(instr >> 22) & 0xf], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(8)) fputs(".wide_indices", fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(11)) fputs(".texel_offset", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 16) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 16) & 0x3F) + i));
            }
            fputs(", ", fp);

//            assert(((instr >> 40) & 0xC0) == 0);
            fprintf(fp, "@");
            for (unsigned i = 0; i < ((instr >> 33) & MASK(3)); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
     break;

    
   case 0x130:

            fputs("VAR_TEX_BUF_SINGLE", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x131:

            fputs("VAR_TEX_BUF_GATHER", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(13)) fputs(".integer_coordinates", fp);
            fputs(valhall_fetch_component[(instr >> 14) & 0x3], fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x132:

            fputs("VAR_TEX_BUF_GRADIENT", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            if (instr & BIT(13)) fputs(".lod_mode", fp);
            if (instr & BIT(14)) fputs(".lod_mode", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x137:

            fputs("VAR_TEX_BUF_DUAL", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x138:

            fputs("VAR_TEX_SINGLE", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x139:

            fputs("VAR_TEX_GATHER", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(13)) fputs(".integer_coordinates", fp);
            fputs(valhall_fetch_component[(instr >> 14) & 0x3], fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x13a:

            fputs("VAR_TEX_GRADIENT", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            if (instr & BIT(13)) fputs(".lod_mode", fp);
            if (instr & BIT(14)) fputs(".lod_mode", fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x13f:

            fputs("VAR_TEX_DUAL", fp);
            fputs(valhall_slot[(instr >> 30) & 0x7], fp);
            if (instr & BIT(39)) fputs(".skip", fp);
            fputs(valhall_sample_and_update_mode[(instr >> 33) & 0x7], fp);
            fputs(valhall_register_type[(instr >> 26) & 0x3], fp);
            fputs(valhall_varying_texture_register_width[(instr >> 24) & 0x3], fp);
            fputs(valhall_dimension[(instr >> 28) & 0x3], fp);
            if (instr & BIT(10)) fputs(".array_enable", fp);
            if (instr & BIT(12)) fputs(".shadow", fp);
            fputs(valhall_lod_mode[(instr >> 13) & 0x7], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

//            assert(((instr >> 40) & 0xC0) == 128);
            fprintf(fp, "@");
            for (unsigned i = 0; i < (((instr >> 36) & MASK(3)) + 1); ++i) {
                fprintf(fp, "%sr%u", (i == 0) ? "" : ":",
                        (uint32_t) (((instr >> 40) & 0x3F) + i));
            }
            fputs(", ", fp);

            va_print_src(fp, instr >> 0, fau_page);
            fputs(", ", fp);

            va_print_src(fp, instr >> 16, fau_page);
     break;

    
   case 0x160:

            fputs("FMA_RSCALE.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x161:

            fputs("FMA_RSCALE_N.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x162:

            fputs("FMA_RSCALE_LEFT.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

    
   case 0x163:

            fputs("FMA_RSCALE_SCALE16.f32", fp);
            fputs(valhall_clamp[(instr >> 32) & 0x3], fp);
            assert((instr & (1ull << 63)) == 0 /* reserved */);
            fprintf(fp, "%s ", valhall_flow[instr >> 59]);

            va_print_dest(fp, (instr >> 40), true);
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 0, fau_page,
                    instr & BIT(38),
                    instr & BIT(39));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 8, fau_page,
                    instr & BIT(36),
                    instr & BIT(37));
            fputs(", ", fp);

            va_print_float_src(fp, instr >> 16, fau_page,
                    instr & BIT(34),
                    instr & BIT(35));
            fputs(", ", fp);

            va_print_src(fp, instr >> 24, fau_page);
     break;

   }
}

void
disassemble_valhall(FILE *fp, const void *code, size_t size, bool verbose)
{
   assert((size & 7) == 0);

   const uint64_t *words = (const uint64_t *)code;

   /* Segment into 8-byte instructions */
   for (unsigned i = 0; i < (size / 8); ++i) {
      uint64_t instr = words[i];

      if (instr == 0) {
         fprintf(fp, "\n");
         return;
      }

      if (verbose) {
         /* Print byte pattern */
         for (unsigned j = 0; j < 8; ++j)
            fprintf(fp, "%02x ", (uint8_t)(instr >> (j * 8)));

         fprintf(fp, "   ");
      } else {
         /* Print whitespace */
         fprintf(fp, "   ");
      }

      va_disasm_instr(fp, instr);
      fprintf(fp, "\n");

      /* Detect branches */
      uint64_t opcode = (instr >> 48) & MASK(9);
      bool branchz = (opcode == 0x1F);
      bool branchzi = (opcode == 0x2F);

      /* Separate blocks visually by inserting whitespace after branches */
      if (branchz || branchzi)
         fprintf(fp, "\n");
   }

   fprintf(fp, "\n");
}

