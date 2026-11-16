#!/usr/bin/env bash
# Everything-enabled host census (TF1, PF1, extent, batching) + frame_model pixel table.
cd $NORD2_WORK/gpu/build/c6f-lowering-20261026
c6n/lease_run.sh c6n/census-final.log python3 build/c6f-lowering-20261026/fsjobs/fsjobs.py --rebuild --tag final --preframe 1 --tile
