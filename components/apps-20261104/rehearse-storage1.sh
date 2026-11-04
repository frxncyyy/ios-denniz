#!/bin/bash
# rehearsal of guest-storage1.sh / -undo on a fake root holding the 4 real bundles from the rootfs mirror
set -e
A=$(cd "$(dirname "$0")" && pwd); B=$A/..; F=$(mktemp -d -p "$A" .rehearse-storage1.XXXXXX); trap 'rm -rf "$F"' EXIT; H="python3 $B/autocycle/hfsx.py $B/mirror/rootfs-mirror.img"
for b in /System/Library/PreferenceBundles/StoragePlugins/PodcastsUsagePlugin.bundle /System/Library/SyncBundles/AirFair.syncBundle /System/Library/UsageBundles/VideosUsage.bundle /System/Library/UsageBundles/MusicUsage.bundle; do $H rget $b $F$b 2>/dev/null | tail -1; done
mkdir -p $F/System/Library/SyncBundles/AirFair2.syncBundle $F/private/var/db; echo keep > $F/System/Library/SyncBundles/AirFair2.syncBundle/Info.plist
run(){ sed -n "/^python3 - <<'GUEST'$/,/^GUEST$/p" "$A/$1" | sed '1d;$d' | sed "s|/work/root|$F|g" | python3 -; }
tree(){ (cd $F && find . -type f -print0 | sort -z | xargs -0 sha256sum | sha256sum | cut -c1-16; find . -type d | sort | sha256sum | cut -c1-16) | tr '\n' ' '; }
T0=$(tree); echo "start: $T0"; (cd $F && find . -type d | sort) > $F.d0
run guest-storage1.sh | grep STORAGE1; for b in PreferenceBundles/StoragePlugins/PodcastsUsagePlugin.bundle SyncBundles/AirFair.syncBundle UsageBundles/VideosUsage.bundle UsageBundles/MusicUsage.bundle; do [ ! -e $F/System/Library/$b ] && [ -f $F/private/var/db/nord2-storage/disabled/System/Library/$b/Info.plist ]; done
[ -f $F/System/Library/SyncBundles/AirFair2.syncBundle/Info.plist ]; [ -f $F/private/var/db/nord2-storage/storage1.json ]
T1=$(tree); run guest-storage1.sh | grep -q 'STORAGE1_INSTALL moved=0'; [ "$T1" = "$(tree)" ]; echo "install ok, second install no-op"
run guest-storage1-undo.sh | grep STORAGE1; (cd $F && find . -type d | sort) | diff $F.d0 - || true; T2=$(tree); echo "undo:  $T2"; [ "$T0" = "$T2" ]; [ ! -e $F/private/var/db/nord2-storage ]
run guest-storage1-undo.sh | grep -q 'STORAGE1_UNDO moved=0'; [ "$T0" = "$(tree)" ]; echo "undo restores the identical tree, second undo no-op"
mkdir -p $F/private/var/db/nord2-storage/disabled/System/Library/UsageBundles/MusicUsage.bundle; run guest-storage1.sh | grep -q STORAGE1_REFUSED; echo "both source and destination present: refused"
rm -rf "$F" $F.d0; echo REHEARSAL_OK
