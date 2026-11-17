# Independent existing-image verification

`verify-image.py` is prepared for the completed live-ToDS image. It has not
been run during preparation. It neither builds a boot image nor accesses a
device, CI, authorization state or gates. It does rebuild the small loader
and strip a separate copy of the successful CI kernel, solely in its new
output directory. Its address-space limit is 1 GiB.

After the parent supplies the final image identity and successful CI identity,
run from the repository root (replace every capitalized value with the exact
independently supplied value):

```sh
python3 research/nord2/wifi-ios-live-tods/ios/verify-image.py \
  --check-existing \
  --source FULL_SOURCE_SHA \
  --ci-run SUCCESSFUL_RUN_ID --attempt EXACT_ATTEMPT \
  --kernel-sha256 KERNEL_SHA256 \
  --image-sha256 FINAL_IMAGE_SHA256 --image-bytes FINAL_IMAGE_BYTES \
  --payload-sha256 FINAL_PAYLOAD_SHA256 \
  --output research/nord2/wifi-ios-live-tods/ios/independent-image-review/final
```

The existing stage `build-result.json` must agree with those arguments and the
exact completed/successful run record, CI checksum list and actual kernel.
No argument is derived automatically from the builder's own report. The new
output directory must not already exist; source/image/payload/baseline/recovery
and tool inputs are hashed before and after verification. No original evidence
is overwritten. A failed run exits nonzero and cannot produce a PASS report;
a retry uses a different new output directory.

Checks performed:

- Independent Android-v2 component boundaries, padding, original header fields,
  recomputed header ID, exact final image size and 32 MiB capacity.
- Original pinned Android Linux image and appended DTS, exact header DTS,
  exact recompressed base rescue container and decompressed tested RAM-ADB
  rescue contents. Separate d9a recovery and RAM-ADB images retain fixed hashes.
- Loader freshly rebuilt from sources byte-equal to the supplied CI commit;
  its entire reserved 2 MiB region must match, including zero padding.
- Exact executed ee8b payload's ADT and full padded command line. The actual
  calibration and sole chosen factory MAC property are checked without printing
  their contents. Linux BSS/payload alignment and source/ramdisk bounds remain
  exact.
- A freshly stripped CI kernel's complete rebased Mach-O segment bytes, table,
  entry point, DRAM addresses, zero padding and ADT layout match the embedded
  payload. Parsing/rebasing intentionally reuses the source-pinned mkpayload
  decoder; the verifier does not invoke payload or image packing.

The private report pins all inputs, segment hashes, loader and verified artifact
identity. It imports no old-stage source/schema/reader approval or authorization.
It does not replace the parent's CI source-tree/ISA/linked review, approve a
flash, or prove runtime association, DHCP lease, router or Internet access.
No final CI/source/image identity has been invented during preparation.
