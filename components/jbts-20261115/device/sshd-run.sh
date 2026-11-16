#!/var/jb/bin/sh
# nord2 jbts1: launchd job com.nord2.jb.sshd (root, RunAtLoad, KeepAlive on failure). OpenSSH from the Procursus prefix, config
# /var/jb/etc/ssh/sshd_config (written by the installer), log /var/jb/var/log/sshd.log. Exit 0 (= launchd leaves it down) when
# /var/jb/etc/nord2-jb/disabled or ssh-disabled exists. Waits (max 120 s) for jbinit's pwd_mkdb: sshd resolves its privilege
# separation user "sshd" and the login users through /var/jb/etc/spwd.db (libiosexec).
C=/var/jb/etc/nord2-jb
if [ -e "$C/disabled" ] || [ -e "$C/ssh-disabled" ]; then exit 0; fi
i=0
while [ ! -e /var/jb/var/run/nord2-jb/ready ] && [ $i -lt 120 ]; do sleep 1; i=$((i + 1)); done
exec /var/jb/usr/sbin/sshd -D -f /var/jb/etc/ssh/sshd_config -E /var/jb/var/log/sshd.log
