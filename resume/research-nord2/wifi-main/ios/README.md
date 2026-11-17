# Private next main-Wi-Fi image and evidence stage

This is a new, inactive staging directory. The fallback is the previously executed
`e0d3a50f7a706f27a8d9fb19bbd6951c85f016e8b179b47b726c331331ec65d5`
image (33,325,056 bytes), plus the independently pinned RAM rescue image. That
boot reached ROM_DONE but the pool probe refused the inherited WFDMA count-512
profile before region18 SET, mapping, or zero/readback. It did **not** produce a
usable DMA pool receipt. The future image must incorporate the separately
reviewed revised preflight and prove the full pool receipt before WFDMA init.
It must also include the separately reviewed battery FIFO low-five-bit
preflight correction for captured `FIFO_STAT=0x840`; this is a shared-IP
source-map inference, not a claim that the upper bits are defined or that the
I2C bus is independently owned. The new image verifier must pin the exact
promoted battery source and marker rather than infer it from this directory.

The next command line adds only the distinct `nord2wifimain=1` opt-in to the
baseline, and retains `nord2wifirom=1`, `nord2wifidma=1`, and `nord2battery=1`.
The existing `nord2wififw=1` means read-only firmware verification and is never
reused as a main-radio gate. Private calibration ADT injection requires
`--enable-nord2wifimain`; the saved device WIFI bytes are checked by hash and
injected into `/chosen/nord2-wifi-calibration` only for this distinct image.
No calibration or MAC bytes are committed in source. The parent owns exact
source promotion, CI, image construction, approval, flashing, and recovery.

All four required gates in this directory start false. A previous CI result,
image hash, or user authorization cannot satisfy this image. There is no copied
runner, prepared payload, image, hardware log, or approval here. The next
image verifier must recheck exact new source/CI/kernel/image identities, the
entire boot command line and product ADT, calibration property bytes, current
ROM and DMA claims, and source-bounded runtime markers before the parent seeks
exact new hardware approval. The fallback identity is reference only.

The strict offline parser accepts a clean initial WFDMA count of **zero or
the observed 512** on each of all 27 selected rings, only when base, base
extension, CIDX and DIDX are zero, both global engines are disabled and stable,
and all 112 preflight reads and raw rows are present. Count 512 is an observed
unprogrammed profile, not a documented reset value. Any programmed pointer,
index, other count, active engine, changing global, missing row, or callback
fault is a refusal or incomplete result as appropriate. It must distinguish
known inherited refusal from verified region18 SET/map/zero/readback success;
neither alone means firmware ready. See the independently reviewed private
`wfdma/preflight/v2` candidate. `parse-evidence.py` checks the frozen native
binding's actual telemetry grammar; the build pin in
`native-schema-required.json` remains false until the parent supplies exact
promoted source/CI/kernel evidence. The parser checks exact
ROM→pool→fresh WFDMA owner→patch→CID40→FORCE_CAL/event+EMI backup→PCCIF4→N9
spans→CID41→bounded SW_SYNC0 FW_READY ordering and reject missing/interleaved,
duplicated, contradictory, or truncated records. A terminal panic may prevent
the final outer line; such a record is incomplete evidence, never a fabricated
FW_READY success. FW_READY is not scan or network connectivity.

No current device operation, CI run, image build, active source change, or
authorization occurs from this directory. Any future RF calibration action
requires a separate exact hardware authorization.
