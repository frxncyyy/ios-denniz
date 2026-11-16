#!/usr/bin/env bash
# Private host Mali .so from the current tree + MRT tile-fetch compile checks (host only).
set -e
G=$NORD2_WORK/gpu
C=$G/build/c6f-lowering-20261026/c7/mrt
mkdir -p $C/src && cp $G/metal/tegu/mali_*.c $G/metal/tegu/mali_*.h $G/metal/tegu/mali_graphics_resources.inc $G/metal/tegu/compiler_trace_sha256.h $G/metal/tegu/pipeline_format.h $G/metal/tegu/nord2_target.h $C/src/ 2>/dev/null || true
bash $G/build/shader-coverage-20261027/tools/build_mali_so.sh $C/src $C/lib
cc -std=c11 -I$C/src $C/check.c $C/lib/libnord2_mali_runtime.so -Wl,-rpath,$C/lib -o $C/check
cat > $C/v.vert <<'V'
#version 450
layout(location=0) in vec4 position;
layout(location=0) out vec4 color;
void main(){gl_Position=position;color=vec4(0.25,0.5,0.75,1.0);}
V
glslangValidator -V $C/v.vert -o $C/v.spv >/dev/null
mk(){ cat > $C/$1.frag; glslangValidator -V $C/$1.frag -o $C/$1.spv >/dev/null; }
mk both0 <<'F'
#version 450
layout(input_attachment_index=0,set=0,binding=0) uniform subpassInput a0;
layout(input_attachment_index=1,set=0,binding=1) uniform subpassInput a1;
layout(location=0) in vec4 color;
layout(location=0) out vec4 r0;
void main(){r0=subpassLoad(a0)*subpassLoad(a1).a+color*0.5;}
F
mk write1 <<'F'
#version 450
layout(location=0) in vec4 color;
layout(location=1) out vec4 r1;
void main(){r1=color;}
F
mk fetch2 <<'F'
#version 450
layout(input_attachment_index=2,set=0,binding=0) uniform subpassInput a2;
layout(location=0) in vec4 color;
layout(location=0) out vec4 r0;
void main(){r0=subpassLoad(a2)+color;}
F
run(){ BIFROST_MESA_DEBUG=shaders $C/check $C/v.spv $C/$1.spv $2 $3 > $C/$1-$2.log 2>&1; echo "$1 tile=$2 exit=$? $(grep RESULT $C/$1-$2.log) ld_tile=$(grep -c LD_TILE $C/$1-$2.log)"; }
set +e
run both0 1 2; run both0 0 2; run write1 0 2; run fetch2 1 2
