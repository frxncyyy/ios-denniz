#!/bin/bash
# Unreferenced commit = HEAD + only encode-cpu-opus memo hunks of render_pipeline.inc + PLANMISS report block.
cd $NORD2_WORK
W=gpu/build/compile-stall-20261026
git diff gpu/metal/tegu/render_pipeline.inc | python3 -c "
import sys
d=sys.stdin.read();head,*hs=d.split('\n@@')
bad=('variant_compile_modules','TEGU_DISABLE_MSAA4','Apple XR','variant_spirv','compile_generic_options(vsBytes','fsBytes,fsLength')
keep=['@@'+h for h in hs if not any(b in h for b in bad)]
t=head+'\n'+'\n'.join(keep)
open('$W/ov_memo.patch','w').write(t if t.endswith('\n') else t+'\n')
print('kept',len(keep),'of',len(hs),file=sys.stderr)"
rm -rf $W/snap3; mkdir -p $W/snap3/gpu/metal/tegu
git show HEAD:gpu/metal/tegu/render_pipeline.inc > $W/snap3/gpu/metal/tegu/render_pipeline.inc
patch -s -p1 -d $W/snap3 < $W/ov_memo.patch || exit 1
grep -q "TEGU_VARIANT_MEMO_WAYS 8u" $W/snap3/gpu/metal/tegu/render_pipeline.inc || sed -i 's/#define TEGU_VARIANT_MEMO_WAYS 2u/#define TEGU_VARIANT_MEMO_WAYS 8u/' $W/snap3/gpu/metal/tegu/render_pipeline.inc
export GIT_INDEX_FILE=$PWD/$W/snap3/index; git read-tree HEAD
b1=$(git hash-object -w $W/snap3/gpu/metal/tegu/render_pipeline.inc); b2=$(git hash-object -w $W/snap2/re.inc)
git update-index --cacheinfo 100644,$b1,gpu/metal/tegu/render_pipeline.inc; git update-index --cacheinfo 100644,$b2,gpu/metal/tegu/render_encoder.inc
echo "snapshot HEAD+memo (encode-cpu-opus, unreferenced)" | git commit-tree $(git write-tree) -p HEAD
