Independent checker prepared; actual-image verdict is **PENDING**. No CI/source,
kernel, payload, new-image identity or approval is inherited from an old stage.
No image/build/runner/device execution was performed during preparation.

The target is `shim/out/nord2-xnu/nord2-ios-wifi-submit-scan-stop23.img`.
Execution requires root's separate completed-artifact notice and independent
exact values for successful CI run/attempt, source40, kernel SHA/bytes, image
SHA/bytes and payload SHA/bytes. `--execute` is intentionally absent below until
those values exist. The checker then runs only the stage's existing-image
verification (`--check-existing`) under1GiB, preserves builder receipts and
stage gates, checks artifactSHA256SUMS and compares actual bytes with those
independent identities. That read-only verification internally reconstructs a
temporary expected loader for byte comparison; it does not build/replace the
prepared image, invoke the image builder/approved runner or access a device.

Checks include Android boot capacity/headerID, loader reserve, stock Linux,
appended/header DTs, exact rescue contents, all kernel payload segments,
command-line and fresh source/linked-marker/reader pins. The preboot ADT must
be byte-identical to executed d9a's calibrated identity ADT; removing only raw6
`chosen/mac-address-wifi0` must give executed6e4's ADT. The property must have
flags0 and equal the pinned calibration bytes without printing those bytes.
Primary d9a, secondary6e4 and RAM rescue identities are recovery facts only,
never carried-forward authorization.

Typed local SECURITY/DATA submission intent must be true, while airACK,
peer-reception, key-readiness and hardware join/networking claims stay false.
The root-selected `wifi_post_failure_scan_handoff_requested` must equal the
explicit `--scan-resume-requested true|false` and fresh schema flag. A requested
handoff also requires its independent source review; the scan-resume hardware
claim remains false. Rejoin/roaming/SAE remain unsupported and first-join-used
semantics remain in force.

Future invocation, after root supplies actual values:

```
python3 -B verify-existing.py --execute \
  --ci-run RUN --ci-attempt ATTEMPT --source-commit SOURCE40 \
  --kernel-sha256 KERNEL64 --kernel-bytes KERNEL_LENGTH \
  --image-sha256 IMAGE64 --image-bytes IMAGE_LENGTH \
  --payload-sha256 PAYLOAD64 --payload-bytes PAYLOAD_LENGTH \
  --scan-resume-requested true
```

`report.json` does not exist at preparation. It is created only after successful
verification of the actual bytes and frozen stage state. A separate final
manifest will pin that report. `prepared-checker-manifest.json` pins only this
pending checker/template, not an image approval.
