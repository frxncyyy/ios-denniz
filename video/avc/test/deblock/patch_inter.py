#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Make a build-directory copy of avc/n2avc_inter.c with the clause-8.4.1.1 P_Skip fix.

Clause 8.4.1.1 (ITU-T H.264 11/2007, p.147) sets mvL0 = (0,0) only when A or B is not available, or when an
inter A/B has refIdxL0 == 0 and mvL0 == (0,0).  It has no "A or B is intra" condition: an intra neighbour has
refIdxL0 == -1 and falls through to the 8.4.1.3 predictor, which treats it as available with ref (0,0).

The tree currently forces (0,0) for an intra A/B (WP04's revert 31bca07 of the lead's edit).  This is a real
encoder bug, not an R2 one; avc/n2avc_inter.c belongs to WP04, so R2's suite patches a COPY under build/ and
reports it.  Once the real file is fixed this script copies it unchanged.

usage: patch_inter.py <src avc/n2avc_inter.c> <dst>
"""
import sys

OLD = '''void
n2avc_inter_skip_mv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy)
{
	int ax = 0, ay = 0, bx = 0, by = 0;
	int a_ok, b_ok;

	a_ok = neighbour_inter(s, mbx - 1, mby, &ax, &ay);
	b_ok = neighbour_inter(s, mbx, mby - 1, &bx, &by);

	/* Clause 8.4.1.1: unavailable A or B, an intra A or B, or an inter A/B
	 * whose vector is (0,0) makes the skip vector (0,0).  An intra
	 * neighbour is "available" in the 6.4.9 sense but the clause names it
	 * explicitly, so it must not fall through to the PMV. */
	if (!a_ok || !b_ok || (ax == 0 && ay == 0) || (bx == 0 && by == 0)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	n2avc_inter_pmv(s, mbx, mby, mvx, mvy);
}'''

NEW = '''void
n2avc_inter_skip_mv(const n2avc_slice *s, int mbx, int mby, int *mvx, int *mvy)
{
	const n2avc_mbinfo *a, *b;

	/* Clause 8.4.1.1 (11/2007), verbatim: mvL0 is (0,0) only when A or B is
	 * not available, or when an inter A/B has refIdxL0 == 0 and mvL0 == (0,0).
	 * An intra neighbour has refIdxL0 == -1 and does NOT force (0,0); the
	 * predictor of 8.4.1.3 treats it as available with refIdx -1, mv (0,0). */
	if (!n2avc_avail(s, mbx - 1, mby) || !n2avc_avail(s, mbx, mby - 1)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	a = &s->mb[mby * s->mb_w + mbx - 1];
	b = &s->mb[(mby - 1) * s->mb_w + mbx];
	if ((a->ref == 0 && a->mvx == 0 && a->mvy == 0) ||
	    (b->ref == 0 && b->mvx == 0 && b->mvy == 0)) {
		*mvx = 0;
		*mvy = 0;
		return;
	}
	n2avc_inter_pmv(s, mbx, mby, mvx, mvy);
}'''


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: patch_inter.py <src> <dst>\n")
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()

    n = text.count(OLD)
    if n == 1:
        open(dst, "w", encoding="utf-8").write(text.replace(OLD, NEW, 1))
        print("patch_inter: copied %s with the 8.4.1.1 P_Skip fix (build-only; WP04's file is untouched)" % src)
        return 0
    if "n2avc_avail(s, mbx - 1, mby)" in text and "n2avc_inter_skip_mv" in text:
        open(dst, "w", encoding="utf-8").write(text)
        print("patch_inter: %s already has the availability-based P_Skip derivation; copied unchanged" % src)
        return 0
    sys.stderr.write("patch_inter: n2avc_inter_skip_mv does not match the expected source; refusing (found %d old blocks)\n" % n)
    return 1


if __name__ == "__main__":
    sys.exit(main())
