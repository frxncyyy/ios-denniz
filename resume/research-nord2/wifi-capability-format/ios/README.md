# Private Wi-Fi capability format stage

Fresh, unprepared stage for `nord2-ios-wifi-capability-format-battery-stop23.img`, cloned only from the executed capability stage's source templates. The executed stage and its failed capture remain immutable. This stage uses choice B: exact supported outer class and capability EID ec permit an opaque inner 16-bit field; debug and unsupported events retain strict policy. It captures only the first 36 header bytes for every event before parsing capability data. No MAC or body is logged or returned.

The command line is exactly the executed capability stage's 474-byte command line, including `nord2ufsprofile=1 nord2fbdiag=1 nord2wificap=1`. The 30-second failure drain, private calibration ADT, e0d3 fallback and RAM rescue are retained. The current phone was restored to e0d3 and human-health confirmed. No scan, interface, association or networking is established.

All CI, source/schema review, prepared image, human exact image, RF and conditional recovery gates remain false. CI run, source commit, kernel identity and new image hash are unknown. Root must independently review and fill these gates. The new runner requires `--authorized-wifi-capability-format-calibration-boot` and `boot-with-wifi-capability-format-calibration`; previous approvals do not authorize it. No image build or device action is part of private verification.

`parse-capability.py LOG` requires paired EVENT and EVENTHEADER rows for every event, with bounded count, exact row syntax, numeric widths, source ordering and correlation of raw header words against class, declared lengths, EID and sequence metadata. It accepts opaque inner values only for capability kind 1 and exact EID ec. Debug retains E000. It delegates the complete receipt to the pinned immutable executed capability parser, which itself pins the independently reviewed main-v2 dependency. Unknown, duplicate, truncated and conflicting records refuse success. Raw header bytes cannot independently validate the unlogged TLV body.

Synthetic tests cover the inherited 13 positive and 163 refusal cases, paired-header equivalents, malformed headers, unknown rows and debug policy. The actual failed 6ddf capture has no header diagnostics and must refuse completion; its inner value is unknown. Synthetic inner values are never hardware evidence.

Run `python3 research/nord2/wifi-capability-format/ios/test-stage.py` only while all gates remain closed. Do not rerun after root opens gates. No git, CI, image build, device action, UFS, boot speed or audio change was performed by this stage author.
