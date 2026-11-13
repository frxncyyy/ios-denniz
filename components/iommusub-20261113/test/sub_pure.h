/* 0409-PURE-BEGIN */
enum { N2IO_SUB_PREP = 0x01, N2IO_SUB_PTBASE = 0x02, N2IO_SUB_TFRP = 0x04, N2IO_SUB_TLB = 0x08, N2IO_SUB_DUMP = 0x10, N2IO_SUB_UNDO = 0x80,
       N2IO_SUB_ALL = 0x9f };
/* Ordered actions for value v; returns their number (0..6) or -22 (EINVAL) / -16 (EBUSY). */
static inline int
n2io_sub_plan(uint32_t v, bool armed, bool prepped, uint8_t act[6])
{
	int n = 0;
	if (v & ~(uint32_t)N2IO_SUB_ALL) { return -22; }
	if ((v & N2IO_SUB_UNDO) && (v & (N2IO_SUB_PREP | N2IO_SUB_PTBASE | N2IO_SUB_TFRP | N2IO_SUB_TLB))) { return -22; }
	if (armed && (v & ~(uint32_t)N2IO_SUB_DUMP)) { return -16; }
	if (v & N2IO_SUB_DUMP) { act[n++] = N2IO_SUB_DUMP; }                 /* read-only, before anything is written */
	if ((v & N2IO_SUB_PREP) || ((v & (N2IO_SUB_PTBASE | N2IO_SUB_TFRP | N2IO_SUB_TLB)) && !prepped)) { act[n++] = N2IO_SUB_PREP; }
	if (v & N2IO_SUB_PTBASE) { act[n++] = N2IO_SUB_PTBASE; }
	if (v & N2IO_SUB_TFRP) { act[n++] = N2IO_SUB_TFRP; }
	if (v & N2IO_SUB_TLB) { act[n++] = N2IO_SUB_TLB; }
	if (v & N2IO_SUB_UNDO) { act[n++] = N2IO_SUB_UNDO; }
	return n;
}
/* 0409-PURE-END */
