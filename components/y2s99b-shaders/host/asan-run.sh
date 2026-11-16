cd $NORD2_WORK/gpu/build/y2s99b-shaders/host
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=0 UBSAN_OPTIONS=print_stacktrace=1
run() { echo "=== $*"; timeout 120 "$@" 2>&1 | grep -E 'ERROR: AddressSanitizer|runtime error|numeric: ok|blur_nir: ok|differ;|translated and compiled in all|SUMMARY' | head -8; echo "rc=${PIPESTATUS[0]}"; }
run ./h-asan/shader99b_numeric_host_test h-asan/shader99b_numeric.bc
run ./h-asan/translator_numeric_host_test h-asan/translator_numeric.bc
run ./h-asan/blur_nir_host_test $NORD2_WORK/gpu/build/ui-corpus-20261025/libraries/11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9.metallib
run ./h-asan/metallib_compile_host_test $NORD2_WORK/gpu/build/ui-corpus-20261025/libraries/407d3280dce565607bf300e8f86ae22a8f299a23c0a1b4d5451f701d729ca535.metallib
run ./h-asan/metallib_compile_host_test $NORD2_WORK/gpu/build/ui-corpus-20261025/libraries/aeb779964e4f758a5d6fede281c526e898ca50bf7f029764580eb93cbe9622c8.metallib
run ./h-asan/metallib_compile_host_test $NORD2_WORK/gpu/build/ui-corpus-20261025/libraries/11c74cc42ce5137b73d44d427f75764f886656d4ddb374422d56b609ed2762d9.metallib
