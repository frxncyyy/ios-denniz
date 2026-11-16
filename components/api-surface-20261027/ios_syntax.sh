#!/bin/bash
# iOS arm64 freestanding -Werror compile of compute_frontend.m with the production flags (output kept outside the shared build dir).
cd "$(dirname "$0")/../.." || exit 1
nice -n 10 clang -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 \
 -Wall -Wextra -Werror -fno-stack-protector -fno-objc-arc \
 -DNORD2_JM_TRANSPORT -DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_METAL_BASE \
 -DTEGU_GRAPHICS_BRIDGE -DTEGU_METAL_FEATURE_INIT -DTEGU_METAL_PROTOCOLS ${NORD2_FRONTEND_DEFINES:-} \
 -include build/tegu-frontend/build-identity.h -Ibuild/tegu-frontend -c metal/tegu/compute_frontend.m \
 -o build/api-surface-20261027/obj/compute_frontend_ios.o
