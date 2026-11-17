# Private capability receive diagnostics

Diagnostic-only option A retains the existing E000 admission policy and therefore may repeat the known error20. It only captures evidence needed to identify the rejected inner header.

Both options capture `inner_type` and exactly36 header bytes in each event after proving the outer software-event class and before the inner-type decision. The capture reads only the already-owned copied frame: RXD24 plus WIFI_EVENT12, ending before capability/debug body byte36. Wrong outer types leave the new header fields invalid/zero, avoiding data-frame MAC disclosure. No TLV body, MAC address, or extra backend read is logged. The new row follows each EVENT row:

`NORD2 WIFI CAP EVENTHEADER: n=N valid=1 bytes=36 inner=0x... raw=W0/W1/W2/W3/W4/W5/W6/W7/W8`

`valid=0 bytes=0` carries no observed inner/header value. The parser requires paired ordered rows, bounded widths/extents, and correlation between raw words and existing declared lengths, outer type, EID, and sequence. Receipt completion still requires the pinned executed main/capability parser and the chosen inner policy. Header metadata completion alone never establishes capability or networking. Old actual failure has no header row and remains incomplete. Any zero/seven/arbitrary value in a synthetic fixture is hypothetical.

The event struct grows20→60 bytes, state4512→5152 bytes; nwt1176/nwi3040/nwfn7472 are unchanged. Default-off gating, same-object/current-worker ownership, deadline3s/eventbudget16/pollbudget3000, descriptor/extent/coherent guards, TLV bounds/known-tag uniqueness, EID matching, captured sequence policy, rearm ordering, common halt, fixed failure drain, and parked completion remain. Normal generated XNU compilation and full CI must be repeated after promotion because the captured event/state layout changes.

`test-host.py` runs ASan/UBSan dispatcher, native adapter, actual current-owner, drain, and header fixtures. Header coverage tests all65536 inner values. `test-parser.py` verifies the proposed offline header contract and refusal cases. All fixtures are synthetic; actual failed packet inner remains unknown and cannot be recovered from the saved console/USB artifacts (`source-evidence.json`). No active source, executed stage, CI, image, or device was changed.
