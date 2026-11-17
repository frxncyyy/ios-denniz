# Offline OPEN candidate

Apply the14-runtime contextual `runtime.patch` and six additive tool/fixture files listed with exact hashes in `promotion.json`. No new runtime TU. Do not overwrite newer DFS/lifecycle source files.

Portable tests (from repository root; all outputs must be fresh directories):

```
python3 tools/test-nord2-wifi-open.py --only codec --output /tmp/open-codec
python3 tools/test-nord2-wifi-open.py --only actual --bearssl /path/to/sweet_bearssl --output /tmp/open-actual
python3 tools/test-nord2-wifi-open.py --only owner --board /path/to/retained/WIFI --bearssl /path/to/sweet_bearssl --output /tmp/open-owner
python3 tools/test-nord2-wifi-open.py --only owner-reverse --board /path/to/retained/WIFI --bearssl /path/to/sweet_bearssl --output /tmp/open-reverse
python3 tools/test-nord2-wifi-open.py --only legacy-codec --output /tmp/open-legacy
python3 tools/test-nord2-wifi-open.py --only regressions --bearssl /path/to/sweet_bearssl --output /tmp/open-wpa
python3 tools/test-nord2-wifi-open-source.py --android-tree /path/to/nord2-current-kernel --output /tmp/open-source
```

Before promotion add `--candidate research/nord2/wifi/open-network-20261023/proposed/shim/nord2-xnu`. The owner runner reuses tracked full hardware/retained-worker fixture machinery and requires an explicit local board input. It does not publish that input. Source oracle requires exact Android commit04d00c2565764c3922c9b4826b5acff1106e533f. BearSSL defaults to the existing CI39158169212 local buildtree location for compatibility with current repository runners; explicit override is supported.

`REPORT.md` describes the strict OPEN contract and known limits. No device, CI, image or authorization changes are included. OPEN functionality is host-tested; no hardware success claim.
