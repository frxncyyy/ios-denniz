#!/bin/bash
# Build the prototype tegu-air-bridge cdylib against the translator copy in m2v-fold (jobs1, nice).
cd $NORD2_WORK/gpu/build/compile-stall-20261026/airx && CARGO_TARGET_DIR=$NORD2_WORK/gpu/build/compile-stall-20261026/airx-target CARGO_BUILD_JOBS=1 nice -n 10 cargo build --release -j1 --offline 2>&1 | tail -5
