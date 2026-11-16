/* Mesa GENXML host oracle for BGRA8 premultiplied source-over. MIT licensed
 * generated packer from pinned PAN_ARCH=10 Mesa tree; no GPU execution. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define PAN_ARCH 10
#include "genxml/v10_pack.h"
#include "pan_format.h"
int main(int argc,char **argv) {
 if(argc!=2)return 2;
 struct MALI_BLEND b={MALI_BLEND_header};
 b.round_to_fb_precision=true;b.load_destination=true;b.srgb=false;
 b.internal.mode=MALI_BLEND_MODE_FIXED_FUNCTION;
 b.equation.rgb.a=MALI_BLEND_OPERAND_A_SRC;
 b.equation.rgb.b=MALI_BLEND_OPERAND_B_DEST;
 b.equation.rgb.c=MALI_BLEND_OPERAND_C_SRC_ALPHA;
 b.equation.rgb.invert_c=true;
 b.equation.alpha.a=MALI_BLEND_OPERAND_A_SRC;
 b.equation.alpha.b=MALI_BLEND_OPERAND_B_DEST;
 b.equation.alpha.c=MALI_BLEND_OPERAND_C_SRC_ALPHA;
 b.equation.alpha.invert_c=true;
 b.equation.color_mask=0xf;
 b.internal.fixed_function.num_comps=4;
 b.internal.fixed_function.conversion.memory_format=GENX(pan_dithered_format_from_pipe_format)(PIPE_FORMAT_B8G8R8A8_UNORM,false);
 b.internal.fixed_function.rt=0;
 assert(b.internal.fixed_function.conversion.memory_format==0xed000u);
 struct mali_blend_packed packed;MALI_BLEND_pack(&packed,&b);
 assert(packed.opaque[0]==0xa01u&&packed.opaque[1]==0xf0d32d32u&&
        packed.opaque[2]==0x1au&&packed.opaque[3]==0xed000u);
 FILE *f=fopen(argv[1],"wb");if(!f)return 3;
 assert(fwrite(packed.opaque,1,sizeof(packed.opaque),f)==sizeof(packed.opaque));
 assert(!fclose(f));
 return 0;
}
