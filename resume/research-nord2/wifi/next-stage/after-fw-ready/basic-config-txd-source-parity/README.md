# Private BASIC TX completion source parity plus captured diagnostics

Actual image89b3 failed BASIC phase3/error17 on the first poll, DIDX5, after one
CID2 publication and one PSE debit. The immutable first log is pinned in
actual-failure.json. **Exact rejected descriptor field and all four words are
UNKNOWN.** The old `control=0` output was initialized state written only after
passing the old guard; it is not a measured zero control word. In the exact
reviewed source/binary, error17 is inside the DDONE1 branch, so observing DDONE1
before an extra echo/control refusal is a source-derived inference, not a new
raw hardware value. No prior log or source can reconstruct the missing words.

The selected Android AXI source constructs SDPtr0/length/LastSec fields before
publication, owns the cell through software ring/cell metadata, and completes a
command by DIDX progress plus DMADONE only (hal_pdma.c2301..2364). It does not
require returned pointer/length/segment/extension fields to echo software.
PacketPa/pBuffer come from the software cell. Selected preallocated AXI uses no
unmapTxBuf/freeBuf callback (axi.c1046..1064; hif.h98=1), so even its optional
SDLen0 unmap use is inactive here. No returned descriptor address is used to
choose a memory operation. The packet's exact writeback pattern remains unknown.

This candidate changes only normal BASIC completion after the existing exact
DIDX-next and successful read of the exact software-owned16B descriptor slot.
Capture all four words/control/consumed metadata first; DDONE1 completes TX and
all other returned bits are opaque metadata, as selected source. DDONE0 keeps
waiting under the existing actual3s/3000-poll bound. There is no invented CID2
response or firmware ACK. `consumed` means exact80000000 only and is diagnostic,
never an acceptance condition. The old self-imposed error17 echo guard is removed
for BASIC only. Existing main/INIT/CAP guards and executed evidence are unchanged.

All prepublication descriptor construction/free-slot checks, software buffer/
ring identity, current worker/lease/native guards, ownership/coherence, DIDX
range/old-or-next restriction, memory/sync bounds, one publication, no-response
semantics,1PSE/0PLE one-way debit/no refund, failure/commonhalt/30s drain and
completed4->5/no-callback/park lifetime remain. Raw SDPtr/SDLen/Ext fields are
never address or length authority. `tx_descriptor_address` and expected buffer
are captured from existing owned software values before I/O; no new backend
operation is introduced. The changed state is the separate BASIC object; shared
native/init/transport/CAP ABI stays7472/3040/1176/5152.

The new prepared-only diagnostic row is:

`NORD2 WIFI BASIC TXDESC: observed=%u address=%#llx expected=%#llx software_used=%u raw=%#x/%#x/%#x/%#x`

`observed` counts successful postpublication owned-slot descriptor copies and is
zero if none occurred. Raw fields/control are captured even when DDONE0. The
existing TX row's `control` now always denotes that last observed control when
observed>0, rather than an accepted-only value. Software_used remains76; address
and expected are owned software metadata, not observed SDPtr values. Descriptor
metadata contains no MAC/TLV/debug payload. Root owns the fresh future stage and
parser; parser-contract.json is a proposal only. Old executed stages are immutable.

`python3 test.py` uses ASan/UBSan and the real current native ownership/lease
policy. Three suites cover the prior native/defaultoff/skip/STA29/30s drain
regressions, all67 callback failures without further callback/retry/refund,134
opaque DDONE1 writebacks (including varied/all-ones pointers/extensions/length/
segment fields), DDONE0 poll and actual-clock deadlines, and old/wrong/out-of-range
DIDX refusals despite DDONE1. All values are synthetic, not the missing actual
writeback. Rejected-field information has not been invented. No normal sibling
compile, CI, image or hardware success is claimed for this candidate.

Only three canonical files are proposed. The unchanged copied CAP binder exists
in proposed/ solely so quoted includes select the candidate BASIC binder during
host tests; it is not part of promotion. runtime.patch contains only the three
changed files. Parent/independent review, normal generated owning-TU compilation,
fresh CI/linked/image checks and exact new-image authorization remain necessary.
No canonical source, git, CI, image, device, gate, NOTES or executed stage was edited.
