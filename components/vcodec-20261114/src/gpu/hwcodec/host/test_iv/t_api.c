/* SPDX-License-Identifier: MIT */
/*
 * t_api.c -- test_iv: the library half of the runner (host/n2vhost.h), against the fake n2vc_hw.
 *
 * Checks: the bring-up sequence and its accessors, the hardware wrapper that produces the M-e/M-f
 * evidence, n2vhost_note()/n2vhost_report(), the error paths, and that a second start/stop cycle is
 * clean (ASan+UBSan and -O2 builds both run this).
 *
 * usage: t_api <vendor-dir> <record-file>
 * Prints "IV-API PASS n/n" and exits 0, or "IV-API FAIL <what>" and exits 1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "n2vhost.h"
#include "n2vc_hw_fake.h"

static int passed, total;
static const char *vendor_dir, *record_path;

static int ck(int cond, const char *what)
{
	total++;
	if (cond) {
		passed++;
		return 1;
	}
	fprintf(stderr, "IV-API FAIL %s\n", what);
	return 0;
}

/* Search a report file for a substring. */
static int report_has(FILE *f, const char *needle)
{
	char line[512];
	int found = 0;

	rewind(f);
	while (fgets(line, sizeof line, f))
		if (strstr(line, needle)) {
			found = 1;
			break;
		}
	rewind(f);
	return found;
}

static int start(struct n2vhost **h, struct n2vc_hw *hw)
{
	*h = NULL;
	return n2vhost_start(h, hw, vendor_dir);
}

int main(int argc, char **argv)
{
	struct n2vc_hw hw;
	struct n2vhost *h = NULL, *h2 = NULL;
	const struct n2vc_hw *w;
	int fake_open = 1;
	FILE *rep = NULL;
	int rc;

	if (argc < 3) {
		fprintf(stderr, "usage: t_api <vendor-dir> <record-file>\n");
		return 2;
	}
	vendor_dir = argv[1];
	record_path = argv[2];

	memset(&hw, 0, sizeof hw);
	if (n2vc_hw_open_fake(&hw, record_path) != 0) {
		fprintf(stderr, "IV-API FAIL cannot open the fake hardware\n");
		return 1;
	}

	/* ---- hard error paths that must not need a hardware back end ---- */
	if (!ck(n2vhost_start(NULL, &hw, vendor_dir) == -1, "n2vhost_start(NULL) returns -1"))
		goto out;
	if (!ck(n2vhost_start(&h2, NULL, vendor_dir) == -1, "n2vhost_start(hw=NULL) returns -1"))
		goto out;
	if (!ck(h2 == NULL, "hardware==NULL leaves *out NULL"))
		goto out;
	if (!ck(n2vhost_start(&h2, &hw, "/nonexistent-vendor-dir") == -1, "bad vendor directory returns -1"))
		goto out;
	if (!ck(h2 != NULL && n2vhost_error(h2) != NULL && n2vhost_error(h2)[0] != '\0',
		"a failed start still reports its error"))
		goto out;
	n2vhost_stop(h2);
	h2 = NULL;
	if (hw.close)
		hw.close(hw.ctx);
	memset(&hw, 0, sizeof hw);
	fake_open = 0;
	if (n2vc_hw_open_fake(&hw, record_path) != 0) {
		fprintf(stderr, "IV-API FAIL cannot reopen the fake hardware\n");
		return 1;
	}
	fake_open = 1;
	if (!ck(n2vhost_report(NULL, stdout) == 0, "n2vhost_report(NULL) is harmless"))
		goto out;

	/* ---- the real bring-up ---- */
	rc = start(&h, &hw);
	if (!ck(rc >= 0, "n2vhost_start succeeds on the vendor set"))
		goto out;
	if (!ck(h != NULL, "n2vhost_start sets *out"))
		goto out;
	if (!ck(n2vhost_mem(h) != NULL && n2vhost_elf(h) != NULL, "mem and elf accessors"))
		goto out;
	if (!ck(n2vhost_unresolved(h) <= 149, "unresolved count is within the import census"))
		goto out;
	if (!ck(n2vhost_milestone(h) >= N2VHOST_MS_A, "M-a reached"))
		goto out;
	if (!ck(n2vhost_milestone(h) >= N2VHOST_MS_B, "M-b reached (init arrays ran)"))
		goto out;

	w = n2vhost_hw(h);
	if (!ck(w != NULL && w != &hw, "the wrapped hardware back end is a copy"))
		goto out;
	if (!ck(w->ctx != hw.ctx, "the wrapper replaces ctx"))
		goto out;

	/* ---- the wrapper must record M-e and M-f while forwarding ---- */
	if (!ck(w->power(w->ctx, RWVC_BLK_VENC, 1) == 0, "wrapped power(VENC, on) forwards"))
		goto out;
	{
		struct rwvc_reg_op op = { RWVC_OP_WRITE, RWVC_WIN_VENC, 0, 0x0010, 0x00000001u, 0 };
		unsigned done = 0;

		if (!ck(w->regs(w->ctx, &op, 1, &done) == 0 && done == 1, "wrapped regs() forwards"))
			goto out;
	}
	if (!ck(n2vhost_milestone(h) == N2VHOST_MS_E, "M-e recorded on the first VENC window access"))
		goto out;
	if (!ck(strstr(n2vhost_milestone_evidence(h, N2VHOST_MS_E), "VENC") != NULL,
		"M-e evidence names the window"))
		goto out;
	{
		uint32_t status = 0;

		(void)w->wait_irq(w->ctx, RWVC_IRQ_VENC, 5, &status);   /* the fake times out: still an M-f */
	}
	if (!ck(n2vhost_milestone(h) == N2VHOST_MS_F, "M-f recorded on wait_irq(RWVC_IRQ_VENC)"))
		goto out;
	n2vhost_note(h, N2VHOST_MS_D, "t_api: INIT_DONE simulated");
	if (!ck(n2vhost_milestone(h) == N2VHOST_MS_F, "a note does not lower the milestone"))
		goto out;

	/* ---- the report ---- */
	rep = tmpfile();
	if (!ck(rep != NULL, "tmpfile for the report"))
		goto out;
	if (!ck(n2vhost_report(h, rep) == (int)N2VHOST_MS_F, "report returns the milestone reached"))
		goto out;
	if (!ck(report_has(rep, "N2VHOST reached M-f:"), "report's last milestone is M-f"))
		goto out;
	if (!ck(report_has(rep, "N2VHOST M-a loaded 5 objects"), "report lists five objects"))
		goto out;
	if (!ck(report_has(rep, "N2VHOST maps "), "report shows the guest /proc/self/maps text"))
		goto out;
	if (!ck(report_has(rep, "N2VHOST M-d t_api: INIT_DONE simulated"), "report shows the noted M-d"))
		goto out;
	if (!ck(report_has(rep, "first VENC window access"), "report shows the M-e evidence"))
		goto out;
	if (!ck(report_has(rep, "N2VHOST stop "), "report shows the guest stop line"))
		goto out;
	if (!ck(n2vhost_stop_text(h) != NULL && n2vhost_stop_text(h)[0] != '\0', "stop text is never empty"))
		goto out;

	/* ---- the numeric stop reason agrees with the text form -------------------------------- */
	{
		int joined = n2vhost_join(h, 3000);
		enum n2a32_stop sr = n2vhost_stop_reason(h);

		if (!ck(joined == 0 ? sr != N2A32_STOP_NONE : sr == N2A32_STOP_NONE,
			"n2vhost_stop_reason: NONE while running, the interpreter enum once the guest ended"))
			goto out;
	}

	/* ---- idempotent teardown, then the documented one-set-per-process refusal --------------------- */
	n2vhost_stop(h);
	h = NULL;
	if (!ck(n2vc_hw_fake_violations(&hw) == 0, "no access to an unpowered window"))
		goto out;
	/* n2vfs (V33) has no unregister and keeps the /dev/vcu nodes of the first set, so a second start in
	 * the same process must fail with a clear message instead of leaving a dead model in the table. */
	{
		struct n2vhost *again = NULL;
		int rc2 = n2vhost_start(&again, &hw, vendor_dir);

		if (!ck(rc2 == -1, "a second start in the same process is refused"))
			goto out;
		if (!ck(again != NULL && strstr(n2vhost_error(again), "one guest set per process") != NULL,
			"the refusal names the n2vfs limitation"))
			goto out;
		n2vhost_stop(again);
	}
	/* n2vhost_stop(NULL) is a no-op */
	n2vhost_stop(NULL);
	if (!ck(1, "teardown is clean"))
		goto out;

out:
	if (rep)
		fclose(rep);
	if (h)
		n2vhost_stop(h);
	if (fake_open && hw.close)
		hw.close(hw.ctx);
	if (passed == total && total > 0) {
		printf("IV-API PASS %d/%d\n", passed, total);
		return 0;
	}
	printf("IV-API FAIL %d/%d\n", passed, total);
	return 1;
}
