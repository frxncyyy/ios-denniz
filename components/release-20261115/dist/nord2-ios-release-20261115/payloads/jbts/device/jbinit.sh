#!/var/jb/bin/sh
# nord2 jbts1: per-boot preparation of the rootless Procursus prefix /var/jb (launchd job com.nord2.jb.init, root, RunAtLoad).
# Installed by gpu/build/jbts-20261115 (guest-jbts1.sh).  Kill switches (files):
#   /var/jb/etc/nord2-jb/disabled           -> this script and the sshd job do nothing
#   /var/jb/etc/nord2-jb/ssh-disabled       -> only sshd stays off
#   /var/jb/etc/nord2-jb/install-trollstore -> present = install TrollStore from the bundled TrollStore.tar once (removed on success)
# Steps (the first three follow what Procursus' prep_bootstrap.sh and HakiOS' tools/mk-jb.sh prep.sh do, see SETUP.md for credits):
#   1. firmware: writes the "firmware" package (Darwin version) into dpkg's database
#   2. pwd_mkdb: /var/jb/etc/{pwd,spwd}.db from master.passwd (libiosexec reads only these: sshd, su, sudo, login)
#   3. once: the bootstrap's postinst scripts (shells, alternatives, _apt user)
#   4. register /var/jb/Applications/*.app with LaunchServices (uicache -p, retried while SpringBoard/lsd start)
#   5. once: TrollStore from the bundled tar (trollstorehelper install-trollstore + install-ldid)
PATH=/var/jb/usr/bin:/var/jb/bin:/var/jb/usr/sbin:/var/jb/sbin:/usr/bin:/bin:/usr/sbin:/sbin
export PATH
C=/var/jb/etc/nord2-jb
RUN=/var/jb/var/run/nord2-jb
SH=/var/jb/usr/share/nord2-jb
TSH=/var/jb/Applications/TrollStorePersistenceHelper.app/trollstorehelper
log() { echo "$(date '+%Y-%m-%dT%H:%M:%S') jbinit: $*"; }

if [ -e "$C/disabled" ]; then log "disabled ($C/disabled): nothing done"; exit 0; fi
mkdir -p "$RUN"
rm -f "$RUN/ready"
log "start (boot $(sysctl -n kern.boottime 2>/dev/null | cut -c1-40))"

/var/jb/usr/libexec/firmware >/dev/null 2>&1; log "firmware rc=$?"
pwd_mkdb -p /var/jb/etc/master.passwd; log "pwd_mkdb rc=$?"

if [ ! -e "$C/postinst-done" ]; then
	for package in debianutils apt dash bash zsh vi; do
		if [ -x "/var/jb/Library/dpkg/info/$package.postinst" ]; then
			"/var/jb/Library/dpkg/info/$package.postinst" configure 999999 >/dev/null 2>&1
			log "postinst $package rc=$?"
		fi
	done
	pwd_mkdb -p /var/jb/etc/master.passwd
	touch "$C/postinst-done"
fi
touch "$RUN/ready"
log "ready (sshd may start)"

# LaunchServices registration of the prefix's applications (Sileo, TrollStore Helper). By their real path in /var/jb, the path
# a later "uicache -a" (dpkg trigger of every package that ships an app) finds again. Retried every 5 s for up to 3 min.
register() {
	app=$1
	try=1
	while [ $try -le 36 ]; do
		out=$(uicache -p "$app" 2>&1)
		case "$out" in
		*rror*|*nable*) ;;
		*) log "registered $app (try $try)"; return 0 ;;
		esac
		sleep 5
		try=$((try + 1))
	done
	log "could not register $app: $out"
	return 1
}

trollstore_installed() {
	for d in /var/containers/Bundle/Application/*/TrollStore.app; do
		[ -d "$d" ] && [ -e "${d%/TrollStore.app}/_TrollStore" ] && return 0
	done
	return 1
}

# Foreground on purpose: launchd kills a job's leftover process group when its main process exits.
sleep 20
	for app in /var/jb/Applications/*.app; do
		[ -d "$app" ] && register "$app"
	done
	if [ -e "$C/install-trollstore" ]; then
		if trollstore_installed; then
			log "TrollStore already installed; gate removed"
			rm -f "$C/install-trollstore"
		elif [ -x "$TSH" ] && [ -f "$SH/TrollStore.tar" ]; then
			try=1
			while [ $try -le 5 ]; do
				timeout 300 "$TSH" install-trollstore "$SH/TrollStore.tar"
				rc=$?
				log "trollstorehelper install-trollstore rc=$rc (try $try)"
				if [ $rc -eq 0 ] && trollstore_installed; then
					"$TSH" install-ldid "$SH/ldid" "$(cat "$SH/ldid.version")"
					log "trollstorehelper install-ldid rc=$?"
					rm -f "$C/install-trollstore"
					break
				fi
				sleep 30
				try=$((try + 1))
			done
		else
			log "TrollStore gate set but $TSH or $SH/TrollStore.tar missing"
		fi
	fi
log "done"
exit 0
