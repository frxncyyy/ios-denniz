# Installing the T02 phone test (main session only)

There are two ways to run the guest runner: a shell/exec path
(`PHONE_EXEC=... run_phone_tests.sh`) or the one-shot launchd job in this
directory. The launchd job needs no shell on the phone and is the fallback
when the 0296 autotest kernel has no exec command.

## Files to stage into the root file system

| Host file | Guest path | Mode |
|---|---|---|
| `jpeg/rwjpeg_tool` (J02 build) | `/usr/local/bin/rwjpeg_tool` | 0755 root:wheel |
| `tests/phone/rwvc_probe` (build from `rwvc_probe.c`) | `/usr/local/bin/rwvc_probe` | 0755 root:wheel |
| `tests/phone/guest-n2hw-tests.sh` | `/usr/local/bin/n2hw_phone_tests` | 0755 root:wheel |
| `tests/phone/com.nord2.hwcodec.tests.plist` | `/System/Library/LaunchDaemons/com.nord2.hwcodec.tests.plist` | 0644 root:wheel |
| `daemon/vcodecd` (hw-ip, only for the daemon stage) | `/usr/local/bin/vcodecd` | 0755 root:wheel |
| `client/n2vcd_cli` (hw-p03, only for the daemon stage) | `/usr/local/bin/n2vcd_cli` | 0755 root:wheel |

Use the main session's normal root-file-system transaction (the same
mechanism as `gpu/video/install/guest-video.sh`). Do not install
`vcodecd`/`n2vcd_cli` until hw-ip/hw-p03 confirm their names and arguments;
the job runs the daemon stage only when `ENABLE_DAEMON=1` and both binaries
exist.

## Run once

```sh
# on the phone, as root (or let launchd do it)
launchctl load -w /System/Library/LaunchDaemons/com.nord2.hwcodec.tests.plist
# after the job has finished (it is RunAtLoad, not KeepAlive):
cat /var/tmp/n2hw/results.txt
cat /var/tmp/n2hw-launchd.err
```

The job writes one `PASS`/`FAIL`/`SKIP` line per test and ends with
`N2HW PHONE PASS n/n` or `N2HW PHONE FAIL n/n`. It does not flash, reset or
power anything off outside the ioctls it makes.

## Remove

```sh
launchctl unload -w /System/Library/LaunchDaemons/com.nord2.hwcodec.tests.plist
rm -f /System/Library/LaunchDaemons/com.nord2.hwcodec.tests.plist
rm -rf /var/tmp/n2hw
```

The test files themselves (`/usr/local/bin/rwjpeg_tool`, `rwvc_probe`,
`n2hw_phone_tests`) can stay for a later run or be removed with the same
transaction.

## Enabling the daemon stage

Only after `40-daemon-encode.md` has real names/arguments:

```sh
# edit the plist EnvironmentVariables: ENABLE_DAEMON=1
# optional: VCODEC_ARGS=--socket /var/run/n2vcd.sock, CLI_ARGS=--socket ...
```

Keep the job one-shot; a failing daemon test must not loop at every boot.
