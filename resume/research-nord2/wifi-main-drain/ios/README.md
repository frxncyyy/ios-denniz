# Private main Wi-Fi failure-drain image stage

This is a new, unprepared offline template for
`shim/out/nord2-xnu/nord2-ios-wifi-main-drain-battery-stop23.img`. The
preceding executed state image was
`0ce012687f2e8266a9ca28edc18101e772ef34db304874b85526a00d279f9ea5`.
The only intended runtime change is a source-reviewed, **30-second bounded
pre-panic failure drain** for captured diagnostics. It does not authorize
radio retries, additional firmware writes, RF calibration changes, or a
different success path. The exact new source, telemetry grammar, CI build,
kernel, image and hardware outcome remain unknown.

The target command line is unchanged from the executed main-state image:
eight cores, UFS2 live, iOS userdata, ROM and DMA ownership, battery probe,
and the distinct `nord2wifimain=1` opt-in. The private WIFI calibration ADT
injection remains the sole property change from the executed e0d3 fallback
ADT. The fallback stays SHA-pinned e0d3 plus the saved RAM rescue.

All four source/CI/schema, prepared-image and exact-authorization gates are
false. The new marker `NORD2 WIFI MAIN DRAIN:` is required in the reviewed
source and loaded kernel before image verification may proceed; its format
and runtime meaning must be pinned by the author before any positive parser
claim. The strict success parser is copied byte-for-byte from the executed
state stage and keeps failed/truncated results unknown. No previous log,
manifest, payload, kernel, prepared image, approval or device run state is
included here. The one-use runner requires a distinct approval scope and
image hash, and explicitly rejects the executed state image hash.

`python3 test-stage.py` is the bounded offline stage guard. Image preflight
and the runner must remain blocked at the false CI gate. The parent alone
reviews and promotes the drain source, builds an exact image, seeks separate
authorization, and handles any hardware run. Nothing in this directory
starts CI, builds, flashes, or touches the device.
