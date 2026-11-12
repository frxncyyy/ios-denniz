#!/bin/bash
# Host tests of /dev/redwood-jpeg: ASan+UBSan, -O2, and TSan for the one-job lock / two-caller tests. Builds only under
# /tmp/n2hw-k01.
#
# Since 0316 (12 Nov 2026) the harness drives the driver through h_sys_ioctl (XNU's ioctl(): kernel buffer, copyout only
# on success), and gpu/hwcodec/kern/bsd/dev/nord2_jpegdev.inc -- the frozen input of the pinned p031x/0310 -- still
# copyin/copyout's the ioctl `data` (the q10ji bug), so it no longer passes on its own. The test therefore runs on the
# file the phone builds: kernel/b + p031x 0310..0316, compiled with p031x/test/t_jpegdec.c (which includes this
# t_jpegdev.c whole, decoder model on). N2K01_LEGACY=1 runs the old standalone build against bsd/dev (expected to FAIL).
set -euo pipefail
H=$(cd "$(dirname "$0")" && pwd)
W=$(cd "$H/../.." && pwd)                         # gpu/hwcodec
P=$W/kern/p031x
T=/tmp/n2hw-k01
rm -rf "$T"
mkdir -p "$T"
if [ "${N2K01_LEGACY:-0}" = 1 ]; then
	CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -I "$W/include")
	nice -n 10 clang "${CFLAGS[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -pthread "$H/t_jpegdev.c" -o "$T/a"
	exec "$T/a"
fi
mkdir -p "$T/tree" "$T/test" "$T/bsd" "$T/kernel"
cp -a "$W/kernel/b/." "$T/tree/"
for n in 0310-nord2-jpegdev-vcodec 0311-nord2-vcodec-venc-autotest 0312-nord2-vcodec-venc-fast 0313-nord2-vcodec-pool-384m \
         0314-nord2-vcodec-adaptive-wait 0315-nord2-jpeg-hw-decode-path 0316-nord2-jpeg-ioctl-kernel-buffer \
         0317-nord2-jpeg-encode-eoi-trim-128; do
	(cd "$T/tree" && patch -p1 -s --no-backup-if-mismatch -i "$P/$n.patch") || { echo "N2TEST K01 FAIL $n does not apply"; exit 1; }
done
ln -s "$T/tree/bsd/dev" "$T/bsd/dev"; ln -s "$T/tree" "$T/kernel/b"
cp "$H/t_jpegdev.c" "$P/test/t_jpegdec.c" "$T/test/"
CFLAGS=(-std=c11 -g -Wall -Wextra -Werror -pthread -iquote "$T/tree/bsd/dev" -I "$W/include")
nice -n 10 clang "${CFLAGS[@]}" -O1 -fsanitize=address,undefined -fno-sanitize-recover=all "$T/test/t_jpegdec.c" -o "$T/a"
"$T/a" -q
nice -n 10 clang "${CFLAGS[@]}" -O2 "$T/test/t_jpegdec.c" -o "$T/b"
"$T/b" -q
nice -n 10 clang "${CFLAGS[@]}" -O1 -fsanitize=thread "$T/test/t_jpegdec.c" -o "$T/c"
"$T/c" -q
