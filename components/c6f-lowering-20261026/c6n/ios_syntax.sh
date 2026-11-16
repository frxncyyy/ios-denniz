#!/usr/bin/env bash
IOS="clang -fsyntax-only -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-objc-arc -DNORD2_JM_TRANSPORT -DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_METAL_BASE -DTEGU_GRAPHICS_BRIDGE -DTEGU_METAL_FEATURE_INIT -DTEGU_METAL_PROTOCOLS"
r=0
for v in "" "-DTEGU_INERT_PIPELINES -DTEGU_COMPOSITOR_DEFAULT -DTEGU_JM_BATCH_DEFAULT" "-DTEGU_RENDER_TRACE -DTEGU_INERT_PIPELINES"; do
 $IOS $v -include build/tegu-frontend/build-identity.h -Ibuild/tegu-frontend metal/tegu/compute_frontend.m || r=1; echo "variant [$v] done r=$r"; done
clang -fsyntax-only --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror -Iinclude -Imetal -Ibuild/tegu-frontend metal/nord2_metal_bridge.c || r=1
clang -fsyntax-only --target=arm64-apple-ios16.0 -isysroot "$PWD/build/apple-toolchain/sdks/iPhoneOS16.5.sdk" -O2 -Wall -Wextra -Werror -Imetal -Ibuild/tegu-frontend metal/nord2_metal_render.c || r=1
exit $r
