Status: the root-reported `bmc_wlan=4` STA fixtures aren't under `tools/` (no `sw_sta` hits there), so I'm locating the actual pure wire/golden fixtures before migrating anything.

Status: there is no literal `.bmc_wlan=4` STA initializer anywhere in the repo, so the fixtures root means likely build STA bytes positionally or via a golden; narrowing to test-named files.

No canonical test calls `sw_sta` directly (only shim sources and a patch), so the STA-BMC dependency must be a byte-level golden. Checking `review-vectors.h`/`test-review.c` and the patch.
