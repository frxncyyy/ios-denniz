# facts/vdec_vld — STATUS (opus-dec2, 2026-11-11, host only)

Reference model of the VDEC VLD barrel shifter / AVC_VLD helpers as the vendor H.264 decoder uses them. Spec and evidence:
`gpu/hwcodec/facts/VDEC_REGISTERS.md` (sections 2-3).

Done
- `n2_vdec_vld.{h,c}`: two units (core 0x20000/0x22000/0x25000, LAT 0x11000/0x12000/0x10000), vfifo ring, rptr latch,
  peek / shift-then-peek / ue / se, emulation-prevention removal, start-code search, init-fetch and SRAM-stable waits, the
  vendor's rptr formula, larb +0x00c sleep ack, decode trigger -> reader moves to the next start code.
- `t_vdec_vld.c`: 280 checks; the driver side repeats the vendor sequences; expected values hand-encoded.
- `bash gpu/hwcodec/facts/vdec_vld/run_tests.sh` -> `N2TEST VDECVLD PASS 2/2` (ASan+UBSan -O1, -O2).

Not done / risks
- Not wired into the fake (T02 hook proposed by DEC03; DEC03 owns the integration in `tests/hw/vdec/`).
- Not run against the vendor library yet: the [I] items of VDEC_REGISTERS.md section 6 (core vs LAT context, EPB in the
  shifter, AVC 0x084 bit 31, 0x2c8 bit 11, `initautofetch` path) need DEC02's trace.
- No pixels, no status registers after "decode done" (0x270/0x274/0x278/0x280/0x284, MISC 0x0a4): left to the fake.
