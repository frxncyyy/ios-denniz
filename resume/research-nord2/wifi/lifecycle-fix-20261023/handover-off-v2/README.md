OFF/ON IOUC ticket amendment

PASS: successful OFF now preserves the old retained cookie in the existing retiring slot. ON followed by Join can reach bounded native handover before old RETIRED; close/cancel retires both independently. No native admission, key, DMA, scan, or PID rule changes.

The patch is relative to the saved three-file baseline. The parent already has the preceding handover changes. Promote only the three listed runtime/test paths; review.json records exact hashes and host-test limits.
