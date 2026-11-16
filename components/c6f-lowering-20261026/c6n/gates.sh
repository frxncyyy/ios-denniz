#!/usr/bin/env bash
# Host-only gates for C6n (extent, MRT load/batch, reorder), jobs1, nice 10.
W=$NORD2_WORK
O=$W/gpu/build/c6f-lowering-20261026/c6n
cd "$W"
busy(){ pgrep -x 'clang|clang-23|cc1|rustc|cargo|ninja|ld|ld.lld' >/dev/null || pgrep -f '^(timeout [0-9]+ )?python3 .*(frame_bench|ca_backboardd_host_test|metal_resource_integration|host_test_vs_texture|producer_plan)' >/dev/null || pgrep -f '^bash tools/run_host_tests' >/dev/null; }
until ! busy && sleep 5 && ! busy; do sleep 5; done
echo "lease $(date -Is)" > $O/gates.status
python3 $W/gpu/build/deepseek-parallel-20261025/board/post.py post --from lowering-opus --to all --subject "HEAVY TAKE: lowering-opus c6n gates (iOS syntax, integration, render harness, CA PF0/PF1, run_host_tests), nice10 jobs1" --body "outputs gpu/build/c6f-lowering-20261026/c6n" >/dev/null
IOS="clang -fsyntax-only -fblocks --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror -fno-stack-protector -fno-objc-arc -DNORD2_JM_TRANSPORT -DTEGU_AIR_BRIDGE -DTEGU_MALI_BRIDGE -DTEGU_METAL_BASE -DTEGU_GRAPHICS_BRIDGE -DTEGU_METAL_FEATURE_INIT -DTEGU_METAL_PROTOCOLS"
(cd gpu && for v in "" "-DTEGU_INERT_PIPELINES -DTEGU_COMPOSITOR_DEFAULT -DTEGU_JM_BATCH_DEFAULT" "-DTEGU_RENDER_TRACE -DTEGU_INERT_PIPELINES"; do nice -n 10 $IOS $v -include build/tegu-frontend/build-identity.h -Ibuild/tegu-frontend metal/tegu/compute_frontend.m > $O/ios-syntax.log 2>&1; echo "ios syntax [$v] rc=$?" >> $O/gates.status; done
 nice -n 10 clang -fsyntax-only --target=arm64-apple-ios16.0 -ffreestanding -O2 -Wall -Wextra -Werror -Iinclude -Imetal -Ibuild/tegu-frontend metal/nord2_metal_bridge.c >> $O/ios-syntax.log 2>&1; echo "ios bridge syntax rc=$?" >> $O/gates.status
 nice -n 10 clang -fsyntax-only --target=arm64-apple-ios16.0 -isysroot "$PWD/build/apple-toolchain/sdks/iPhoneOS16.5.sdk" -O2 -Wall -Wextra -Werror -Imetal -Ibuild/tegu-frontend metal/nord2_metal_render.c >> $O/ios-syntax.log 2>&1; echo "ios render syntax rc=$?" >> $O/gates.status)
nice -n 10 python3 gpu/tests/metal_resource_integration_test.py --candidate > $O/integration.log 2>&1; echo "integration rc=$?" >> $O/gates.status
nice -n 10 python3 gpu/build/deepseek-parallel-20261025/workers/render/host_test_vs_texture.py > $O/render-harness.log 2>&1; echo "render-harness rc=$?" >> $O/gates.status
nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-preframe0.log 2>&1; echo "ca preframe0 rc=$?" >> $O/gates.status
NORD2_METAL_PREFRAME=1 nice -n 10 python3 gpu/build/c6f-lowering-20261026/ca_backboardd_host_test.py > $O/ca-preframe1.log 2>&1; echo "ca preframe1 rc=$?" >> $O/gates.status
(cd gpu && nice -n 10 bash tools/run_host_tests.sh > $O/host-tests.log 2>&1); echo "host-tests rc=$? (final probe step needs NORD2_AIR_LLVM_BIN)" >> $O/gates.status
echo "done $(date -Is)" >> $O/gates.status
python3 $W/gpu/build/deepseek-parallel-20261025/board/post.py post --from lowering-opus --to all --subject "HEAVY RELEASE: lowering-opus c6n gates done" --body "-" >/dev/null
