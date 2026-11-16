#!/var/jb/bin/sh
# nord2 ipainbox1/2: install every IPA dropped into /var/mobile/Documents/ipa-inbox with TrollStore's helper (launchd job
# com.nord2.jb.ipainbox, root, RunAtLoad + WatchPaths on the inbox). No network and no UI needed: the PC transaction
# (txn_ipainbox.sh) or scp/sftp (user mobile) puts the files there.
#   ipa-inbox/result.log      one line per IPA: rc + meaning, then the helper's last output lines
#   ipa-inbox/done/<name>     installed;   ipa-inbox/failed/<name>.rcN   not installed (N = trollstorehelper exit code)
#   ipa-inbox/force           present = "trollstorehelper install force" (replaces an app TrollStore did not install, rc 171).
#                             It does NOT bypass rc 179 (same bundle id as an app in /Applications): remove that app first
#                             (PC: txn_ipainbox.sh APPS_UNDO=..., the legit undo path).
# Kill switch: /var/jb/etc/nord2-jb/disabled or /var/jb/etc/nord2-jb/ipainbox-disabled.
PATH=/var/jb/usr/bin:/var/jb/bin:/var/jb/usr/sbin:/var/jb/sbin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH
I=/private/var/mobile/Documents/ipa-inbox
C=/var/jb/etc/nord2-jb
TSH=/var/jb/Applications/TrollStorePersistenceHelper.app/trollstorehelper
L=$I/result.log
[ -e "$C/disabled" ] || [ -e "$C/ipainbox-disabled" ] && exit 0
[ -d "$I" ] || exit 0
mkdir -p "$I/done" "$I/failed"
if ! mkdir "$I/.lock" 2>/dev/null; then
	old=$(cat "$I/.lock/pid" 2>/dev/null)
	if [ -n "$old" ] && kill -0 "$old" 2>/dev/null; then exit 0; fi     # another run is busy
	rm -rf "$I/.lock"; mkdir "$I/.lock" || exit 0
fi
echo $$ > "$I/.lock/pid"
trap 'rm -rf "$I/.lock"; chown -R 501:501 "$I/done" "$I/failed" "$L" 2>/dev/null' EXIT
log() { echo "$(date '+%Y-%m-%dT%H:%M:%S') $*" >> "$L"; }
meaning() {
	case $1 in
	0) echo "installed" ;;
	124) echo "timeout (900 s): helper still busy or hung (installd step? try TrollStore Settings > Install Method: Custom)" ;;
	167) echo "no .app inside the IPA's Payload/" ;;
	171) echo "an app with this bundle id is installed and is not a TrollStore app: create ipa-inbox/force to replace it" ;;
	172|176) echo "Info.plist missing or incomplete" ;;
	173) echo "ldid missing (nord2-jb trollstore)" ;;
	175) echo "ldid signing failed" ;;
	179) echo "same bundle id as an app outside /var/containers (e.g. /Applications): remove that app first (PC apps undo)" ;;
	180) echo "main binary is FairPlay-encrypted: use a decrypted IPA" ;;
	181) echo "LaunchServices registration failed" ;;
	182) echo "installed, needs developer mode (harmless on this port)" ;;
	184) echo "installed, some extra binaries are encrypted (may not work)" ;;
	*) echo "trollstorehelper error" ;;
	esac
}
trollstore_ready() {
	for d in /var/containers/Bundle/Application/*/TrollStore.app; do
		[ -d "$d" ] && [ -e "${d%/TrollStore.app}/_TrollStore" ] && [ -e "$d/ldid" ] && return 0
	done
	return 1
}
# give SpringBoard/lsd time after a boot
boot=$(sysctl -n kern.boottime 2>/dev/null | sed 's/.*sec = \([0-9]*\).*/\1/'); now=$(date +%s)
case "$boot" in ''|*[!0-9]*) boot=0 ;; *) up=$((now - boot)); [ $up -lt 120 ] && sleep $((120 - up)) ;; esac
# Registration check (ipainbox2): LaunchServices loses apps under /var/containers on every full database rebuild (LS caches moved
# aside by an install/undo payload; lsd jetsam-killed mid-rebuild before kernel 0732). Once per boot, and again after installs:
# wait until lsd answers (uicache -l lists >= 20 apps, max 5 min), then for every TrollStore bundle (container with _TrollStore):
# listed? else "trollstorehelper refresh", else "uicache -p <app>"; each step and the final state go to result.log.
ts_apps() {
	for c in /var/containers/Bundle/Application/*; do
		[ -e "$c/_TrollStore" ] || continue
		for a in "$c"/*.app; do [ -d "$a" ] && echo "$a"; done
	done
}
IDS=/var/jb/etc/nord2-jb/ipainbox-ids    # "<Name>.app <bundle id>" lines, written by the payload (no plist tool in the prefix)
bid_of() { awk -v n="$(basename "$1")" '$1 == n { print $2; exit }' "$IDS" 2>/dev/null; }
ensure_registered() {
	why=$1; n=0
	while [ $n -lt 30 ]; do
		cnt=$(uicache -l 2>/dev/null | wc -l)
		[ "$cnt" -ge 20 ] && break
		sleep 10; n=$((n + 1))
	done
	log "registration check ($why): LaunchServices lists $cnt apps"
	refreshed=0
	for a in $(ts_apps); do
		b=$(bid_of "$a")
		[ -n "$b" ] || { log "  $(basename "$a"): bundle id unknown (not in $IDS), not checked"; continue; }
		if uicache -l 2>/dev/null | grep -q -w -F "$b"; then continue; fi
		if [ $refreshed = 0 ]; then
			"$TSH" refresh >/dev/null 2>&1; log "  $b not registered: trollstorehelper refresh rc=$?"; refreshed=1; sleep 3
		fi
		if ! uicache -l 2>/dev/null | grep -q -w -F "$b"; then
			out=$(uicache -p "$a" 2>&1); log "  $b still missing: uicache -p rc=$? ${out:+($out)}"
		fi
		uicache -l 2>/dev/null | grep -q -w -F "$b" && log "  $b registered" || log "  $b NOT registered"
	done
}
# give SpringBoard/lsd time after a boot (above), then once per boot
M=/var/jb/var/run/nord2-jb/ipainbox-checked
if [ "$(cat "$M" 2>/dev/null)" != "$boot" ] && trollstore_ready; then
	ensure_registered "boot $boot"
	mkdir -p /var/jb/var/run/nord2-jb; echo "$boot" > "$M"
fi
set -- "$I"/*.ipa "$I"/*.tipa
have=0; for f in "$@"; do [ -f "$f" ] && have=1; done
[ $have = 1 ] || exit 0
# wait (max 10 min) for TrollStore + ldid (jbinit installs them at the first boot)
i=0; while ! trollstore_ready && [ $i -lt 60 ]; do sleep 10; i=$((i + 1)); done
trollstore_ready || { log "TrollStore (with ldid) not installed: nothing done (nord2-jb trollstore)"; exit 0; }
installed=0
for f in "$@"; do
	[ -f "$f" ] || continue
	n=$(basename "$f")
	s1=$(wc -c < "$f"); sleep 5; s2=$(wc -c < "$f")
	[ "$s1" = "$s2" ] || { log "$n still growing ($s1 -> $s2 B), next run"; continue; }
	opt=""; [ -e "$I/force" ] && opt=force
	log "install $n ($s1 B) ${opt:+[force]}"
	out=$(timeout 900 "$TSH" install $opt "$f" 2>&1); rc=$?
	log "result $n rc=$rc: $(meaning $rc)"
	[ -n "$out" ] && echo "$out" | tail -n 15 | sed 's/^/    | /' >> "$L"
	case $rc in
	0|182|184) mv -f "$f" "$I/done/$n"; installed=1 ;;
	*) mv -f "$f" "$I/failed/$n.rc$rc" ;;
	esac
done
[ "$installed" = 1 ] && ensure_registered "after installs"
exit 0
