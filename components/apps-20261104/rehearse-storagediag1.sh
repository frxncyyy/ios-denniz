#!/bin/bash
# rehearsal of guest-storagediag1.sh on a fake root with the real Preferences reports from the rootfs mirror
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-storagediag1.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
$H get /private/var/mobile/Library/Logs/CrashReporter $F/private/var/mobile/Library/Logs/CrashReporter Preferences >/dev/null 2>&1
touch $F/private/var/mobile/Library/Logs/CrashReporter/Preferences-1969-12-31-161232.0002.ips
mkdir -p $F/System/Library/PreferenceBundles/StoragePlugins $F/System/Library/UsageBundles $F/System/Library/SyncBundles
B0=$(cd $F && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum)
sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/guest-storagediag1.sh" | sed '1d;$d' | sed "s|/work/root|$F|g" | python3 - > $F.out
grep -c '^SDIAG_REPORT' $F.out; grep -q 'SDIAG_THREAD Preferences-1969-12-31-161232.0002.ips t7 FAULT MediaLibraryStorageController.workQueue :: PodcastsUI+0x8dda8' $F.out
grep -o '^SDIAG_THREAD Preferences-1969-12-31-161232.0002.ips t7 FAULT.\{0,120\}' $F.out
[ "$B0" = "$(cd $F && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum)" ]; echo "fake root unchanged"
rm -f $F.out; echo REHEARSAL_OK
