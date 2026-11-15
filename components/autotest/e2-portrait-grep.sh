#!/bin/bash
# What E2 (scenario-portrait-e2.txt) says about Portrait.  usage: e2-portrait-grep.sh runs/e2-portrait   (reads OUTDIR/kernel.log)
set -u
D=${1:?run dir}; L=$D/kernel.log; [ -f "$L" ] || L=$(ls $D/*log* | head -1)
echo "== mode strip states (label width: PORTRAIT ~197, PHOTO 125-142)"; grep -h -o '"label_width": [0-9]*[^}]*"mode": "[a-z-]*"' $D/steps.jsonl 2>/dev/null | sed 's/\\//g'
sec() { echo; echo "== $1"; shift; grep -a -n -E "$@" "$L" | grep -v 'NORD2_RW' | cut -c1-320 | head -40; }   # (driver lines left out: PSO/FUNC spam)
secg() { echo; echo "== $1"; shift; grep -a -n -E "$@" "$L" | cut -c1-320 | head -40; }
sec "session configuration with depth / portrait (client side)" 'depth:[1-9]|DepthData|depthDataDelivery|PortraitEffectsMatte|portraitEffect|SemanticSegmentation|Portrait Lighting'
sec "capture device / format depth support asked or refused" 'supportedDepthDataFormats|DepthDataFormat|depth companion|activeDepthDataFormat|isDepthDataDeliverySupported|Disparity|disparity'
sec "Portrait in Camera.app (CAM*)" '\|Camera\|.*[Pp]ortrait|CAMCaptureCapabilities|portraitMode|CAMPortrait|shallowDepth|depthEffect'
sec "matte / segmentation / Neural Engine / CoreML" 'Matte|Espresso|ANE|aned|CoreML|MLModel|VNGenerate|PersonSegmentation|e5rt'
sec "errors around it" 'err:-?[1-9]|-12780|-11800|-16800|-1180[0-9]|NSInvalidArgument|exception|Can.t add|nil (node|input|output)|PROPERTY ERROR'
sec "still pipeline outcome" 'Delivering still image|captureID|processSequence|_prepareOutput|GNR1|np GNR1'
secg "GPU faults / refusals (driver)" 'np GNR1 FAULT|GNR1 FAILED|did not finish|dispatch failed|fault=[1-9]|RW E '
echo; echo "== screenshots"; ls $D/*.png 2>/dev/null
