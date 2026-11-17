# Final-capture wrapup addendum

## Evidence updates

From `../FINAL-CAPTURE-ADDENDUM.md` and `../ADDITIONAL-SOURCE-FACTS.md` (read only):
- **Late GTK renewal now works on hardware** (install, then G2 TX without reinstall), so no rekey change is needed. MIC success alone doesn't show that the frame arrived encrypted under the PTK.
- **Still no matched DHCP.** iOS shows 169.254 and an empty Router field.
- **Rate 0 is valid** (CCK 1M long). AUTO-rate and source-fixed-rate DHCP/ARP both fail, so the failure is not AUTO-specific.
- **M2/M4 (unprotected) get TXS status 0; ordinary protected data fails.**
- **Shared PID127 still prevents per-packet attribution.**
- **Wire's final primary-source audit agrees on STA BMC 255** and found no TXD/TXP/rate defect.

## Conclusion

- The STA-record BMC = 255 correction (`cnm_mem.c:1141-1142`) **remains source-proven and unchanged**.
- **Whether it causes or cures the DHCP failure is not established.** Only the next hardware run can show that, and network success is not promised.

## Parent execution proof (reported by root, not by me)

- **All nine canonical station groups passed** against the initial three-file candidate (`research/nord2/wifi-ios-dhcp/ios/local-tests/claude-station-regression`, session 98767 closed 0). So **no fixture migration is needed**, and canonical fixtures are preserved. The earlier fixture concern was an anticipated risk, not an observed failure.
- `initial-freeze/tests/run.py`, as reported by root:
  - candidate passed with ASan/UBSan (8882 checks);
  - the oracle passed (255, both callers, layout);
  - the baseline aborted at the intended codec assertion, not with a sanitizer error.

## This turn: test-only change

See `CHANGELOG.md`. In short:
- **`tests/test-sta-bmc.c`** now exits **77** with one exact marker only for the intended mismatch: stale BMC 4 accepted and serialized by the codec, and BMC 4 published by native STA1.
- **`tests/run.py`** requires exactly that exit code and marker, with no sanitizer output, for the negative control. Any other failure fails the suite.
- The runtime candidate, canonical files and `initial-freeze` are untouched.

## Parent requirement

Nothing above has been executed by me. Root/Codex needs to rerun `python3 -I research/nord2/wifi/claude-opus-dhcp-08e6/tests/run.py --log-dir <new dir> --regressions` and confirm all of:
- `candidate_ok=true`
- `baseline_expected_mismatch=true` (baseline exit 77 with the mismatch marker)
- the oracle and baseline oracle pass
- the station, normal-tx and gtk-rekey regressions pass
