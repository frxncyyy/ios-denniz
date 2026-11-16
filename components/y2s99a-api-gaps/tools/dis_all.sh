#!/bin/bash
# disassemble a list of 20H392 shared-cache images (read-only) for the capability-query audit
D=$NORD2_WORK/gpu/build/y2s99a-api-gaps/audit/dis
cd $HOME/dsc-work
for i in CoreImage MPSCore MPSImage MPSNeuralNetwork MPSMatrix MPSNDArray MetalPerformanceShaders MPSFunctions MPSBenchmarkLoop RenderBox ImageIO PosterKit PosterBoardServices WallpaperKit MetalKit SceneKit CoreUI VectorKit PhotosUICore VideoToolbox MediaToolbox CoreVideo SpringBoardFoundation SpringBoardUIServices Metal MetalFX Espresso Vision UIKitCore AppleCV3D CoreMedia MapKit PhotoImaging NeutrinoCore AVFCore ARKitCore SpriteKit; do
  [ -s $D/$i.txt ] && continue
  timeout 900 ipsw-dl/x/ipsw dyld disass dsc-full/dyld_shared_cache_arm64 -i $i --no-color > $D/$i.txt 2>$D/$i.err
  echo "$i $? $(wc -l < $D/$i.txt)"
done
echo DONE
