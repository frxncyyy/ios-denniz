# Public Metal compositor probe

`compositor_public_probe.c` compiles a public Metal PSO from a local QuartzCore
metallib and draws the captured compositor pair using proof-derived input bytes.
The source does not contain the metallib or generated facts. Build on a host
with clang and `ld64.lld`, without an Apple SDK:

```sh
python3 tools/tegu/metal/build_compositor_public_probe.py \
  --proof-directory research/compositor-resource-raw-proto-r1/gradient-r3/bootable-r3/out-r3 \
  --metallib /path/to/QuartzCore.metallib \
  --work /tmp/compositor-public-probe-build \
  --stubs tools/tegu/metal/libSystem-ios-arm64.tbd
```

The output executable is not run by the builder. Pass the metallib pathname as
a literal launch argument when preparing the root image, for example:

```sh
python3 tools/tegu/metal/prepare_probe_root_vm.py ... \
  --probe /tmp/compositor-public-probe-build/tegu-compositor-public-probe \
  --probe-argument=--supervise \
  --probe-argument /private/var/mobile/QuartzCore.metallib
```

The preparer forwards argv through a JSON string array and verifies the exact
launchd `ProgramArguments` list after installation. `install_native_probe.py`
also accepts repeated `--probe-argument` options for direct installer use.
With no option, the original one-element program argument list is preserved.
No shell parsing or quoting is applied to argument values.

To build a separate diagnostic A/B variant that directly loads the installed
Tegu bundle, add `--direct-bundle-device` to the build command. The generated
provenance records `device_creation_mode=direct_bundle`; the default build
still uses `MTLCreateSystemDefaultDevice` and records `system_default`.
Direct mode loads `/System/Library/Extensions/TeguMetal.bundle` with
`NSBundle`, logs its resolved bundle and executable paths, and requires its
principal class to be the identical `TeguComputeDevice` class whose superclass
is `_MTLDevice` before calling `alloc/init`. This separates bundle loading and
device construction from system discovery for diagnosis. It does not prove
system registration, BackBoard discovery, UI support, or GPU execution, and it
does not replace the system-device path as the product goal. The runtime still
accepts the same metallib and optional memory-limit arguments.

For a narrower loader-boundary experiment, add
`--direct-dlopen-preflight` together with `--direct-bundle-device`. The builder
rejects the preflight option alone. This variant obtains the bundle's executable
path, durably logs a begin marker, calls `dlopen` with `RTLD_NOW|RTLD_LOCAL`,
captures and logs `dlerror` immediately on failure, and retains a successful
handle for the process lifetime. Only after that does it call the existing
`NSBundle loadAndReturnError:` and class-identity path. A successful preflight
followed by a blocked NSBundle call separates those observed boundaries; it
does not by itself prove system Metal discovery or GPU execution. The option is
off by default and does not alter the ordinary direct-bundle or system-default
builds.

The probe writes bounded records with `vsnprintf` and `write`, handling short
writes and Darwin `EINTR`. Each record is appended to
`/var/log/tegu-compositor-public-probe.log` and `fsync`ed before it is mirrored
to stdout. The durable file is authoritative. If a console write fails, the
probe appends a bounded diagnostic with the errno and failed record, disables
further console writes, and continues while durable file writes remain valid.
An open, write, `fsync`, or close failure on the logfile remains fatal and stops
work at the next phase boundary. The first failed operation and its errno are
sticky, so a later cleanup error cannot replace the original cause. At
termination the probe attempts to append a durable `VERDICT FAIL durable_log`
record even after an earlier logger failure. The terminal call still returns
failure if that marker cannot be committed. A trustworthy final verdict
therefore requires a clean terminal completion record; finding a PASS substring
alone is insufficient, because a failed PASS flush may have written bytes
before reporting an error. The final `PASS` is accepted only after its durable
append succeeds; console loss alone does not turn a successful readback into a
failed verdict. `--replay` reads and prints that log, without
loading Objective-C, Metal, QuartzCore, or changing the memory limit. For a
follow-up boot, pass `--probe-argument=--replay` without a metallib argument;
the installer forwards that as one literal argv element. The probe also
accepts `--replay` after a metallib pathname for direct launchd invocation.

For an opt-in parent supervisor, make `--supervise` the first probe argument,
as shown above. It forks before Objective-C/Metal initialization; the child
runs the same probe argv without that switch. The parent durably records the
spawned PID before `wait4`, then records raw status and decoded exit or signal.
Fork/wait failures, signal death, nonzero child exit, or durable-log errors yield a
failure verdict. Only the child can emit the GPU-specific `PASS`; the parent
does not infer GPU success from process exit alone. Direct runs without the
switch and replay-only invocation keep their existing paths.

Offscreen diagnosis is an opt-in clone mode, not the UI configuration:

```sh
python3 tools/tegu/metal/prepare_probe_root_vm.py ... \
  --probe /tmp/compositor-public-probe-build/tegu-compositor-public-probe \
  --probe-argument /private/var/mobile/QuartzCore.metallib \
  --offscreen-ui
```

The installer requires exactly one `com.apple.backboardd` and one
`com.apple.SpringBoard` job in `LaunchDaemons`; it sets `Disabled=true` only on
those two entries. The read-only guest remount checks the complete plist delta
against its saved original, validates the manifest at
`/var/db/tegu-compositor-offscreen-ui.json`, and reports its SHA-256. Without
`--offscreen-ui`, the guest verifies those original UI job dictionaries are
unchanged. The probe's final verdict is only its own output/readback check; it
does not claim native GPU or display presentation success.

For the full QuartzCore library, the probe can take an optional memory argument
of 128..1024 MiB to set **only its own** active/inactive fatal memory limits
through XNU's `memorystatus_control`. With `--supervise`, the parent validates
and applies that cap to itself before forking; the child then validates and
applies the same cap to itself. Each process logs its role/PID and the original
and effective limits, and refuses to continue if its own change fails. Append
`--probe-argument 512` after the metallib argument in the preparer invocation
to request this for both processes. With no memory argument there is no
automatic cap change. This diagnostic setting does not change any other
process or disable system memory-pressure handling. The command and structure values match
`bsd/sys/kern_memorystatus.h`; the log-I/O flags and `EINTR` value match
`bsd/sys/fcntl.h` and `bsd/sys/errno.h` in the active XNU buildtree.
