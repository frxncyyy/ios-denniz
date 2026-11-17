Offline only: `python3 research/nord2/wifi-emi-legacy/ios/decode-raw-regions.py STRICT_REPORT.json` prints JSON. Input must be the existing strict parser's complete128-return report; partial/error/missing/invalid captures are refused. No device or image action.

Source interpretation follows `research/nord2/wifi/next-stage/monitor-abi/permission-interpretation.md`: SA/EA base40000000, shift16, DRAM last23fffffff; all32 candidate intervals and16 source domain fields preserved. Input capture hash, strict report bytes hash and fullraw x0..3 provenance retained. APC high bits remain uninterpreted, code7 unknown/reserved. No enable/disable, lock, overlap priority or effective permission claim.

CONN intersections use8fb00000+450000. MCIF only tests whether interval includes base8a000000: shared MCIF size is not proven here; do not equate whole CCCI reservation with exact shared window. Constant0/2 observations flagged uninformative, never treated as disabled or permission proof.

`test-decode-raw-regions.py` is pure synthetic focused validation of intervals/bounds/partial refusal/domain7/raw provenance and constant patterns. Existing parser/build/runner/identity files unchanged. No real capture decoded here.
