#!/bin/bash
# V33 host tests: n2vfs (guest fds, devices, virtual /proc and /sys), stdio, time and process-exit shims.
# Builds only under /tmp/n2hw-v33.  Last line on success: N2TEST V33 PASS <passed>/<total>
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
S=$(cd "$H/.." && pwd)
W=$(cd "$S/.." && pwd)
OUT=/tmp/n2hw-v33
rm -rf "$OUT"
mkdir -p "$OUT" "$OUT/dump/data/local/tmp" "$OUT/dump/sdcard"

# V30's formatter is a real dependency (n2fmt/n2fmt_vscan), V02's n2a32_mem is the real guest address space.
SRC="$S/n2vfs.c $S/n2shim_stdio.c $S/n2shim_format.c $W/emu/n2a32_mem.c $W/emu/n2a32_stub_classes.c $H/t_main.c"
INC="-I$W/include -I$S -I$H"
CFLAGS="-std=c11 -Wall -Wextra -Werror $INC"

run() {
	local name=$1
	shift
	if ! nice -n 10 "$@" > "$OUT/$name.log" 2>&1; then
		echo "N2TEST V33 FAIL $name"
		tail -n 40 "$OUT/$name.log"
		exit 1
	fi
	# the last line of a passing run is its N2TEST line; keep only that on the console
	tail -n 1 "$OUT/$name.log"
}

echo "== the table must list every stdio import of the vendor set exactly once"
python3 - "$W" <<'PY'
import re, sys, os
w = sys.argv[1]
src = open(os.path.join(w, "shim", "n2shim_stdio.c")).read()
names = re.findall(r'\{\s*"([^"]+)",\s*[A-Za-z0-9_]+\s*\}', src)
expect = """__errno __fread_chk __open_2 __stack_chk_fail abort clock_gettime close exit fclose feof fflush
fgets fileno fopen fprintf fread fscanf fseek fsync fwrite gettimeofday ioctl open printf puts read
write""".split()
if sorted(names) != sorted(expect):
    print("table mismatch: missing", sorted(set(expect) - set(names)), "extra", sorted(set(names) - set(expect)))
    sys.exit(1)
if len(names) != len(set(names)):
    print("duplicate table entry")
    sys.exit(1)
print("N2TEST V33-TABLE PASS %d/%d" % (len(expect), len(expect)))
PY

echo "== the fatal path calls V32's n2shim_thread_request_stop (strong stand-in)"
nice -n 10 clang $CFLAGS -O2 -fsanitize=address,undefined -fno-sanitize-recover=all \
	$S/n2vfs.c $S/n2shim_stdio.c $S/n2shim_format.c $W/emu/n2a32_mem.c $W/emu/n2a32_stub_classes.c \
	$H/t_hook.c -o "$OUT/t_hook" -lpthread
run hook "$OUT/t_hook"

echo "== build: ASan+UBSan -O1"
nice -n 10 clang $CFLAGS -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	$SRC -o "$OUT/t_asan" -lpthread
run asan "$OUT/t_asan"

echo "== build: -O2 (no sanitizers)"
nice -n 10 clang $CFLAGS -O2 $SRC -o "$OUT/t_o2" -lpthread
run o2 "$OUT/t_o2"

echo "== build: TSan -O1 (per-thread guest errno)"
nice -n 10 clang $CFLAGS -O1 -g -fsanitize=thread $SRC -o "$OUT/t_tsan" -lpthread
run tsan "$OUT/t_tsan"
