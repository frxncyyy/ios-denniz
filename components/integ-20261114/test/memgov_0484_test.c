/* 0484 host test: class rule (only third-party applications), protected names from the logs, kill budget 3 / 60 s then 120 s pause.
 * cc -std=c11 -Wall -Wextra -Werror -I<chain>/bsd/kern memgov_0484_test.c */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "nord2_memgov.h"
static int fails;
#define CHECK(c, m) do { if (!(c)) { fails++; printf("FAIL %d %s\n", __LINE__, m); } } while (0)
int main(void)
{
	CHECK(n2mg_path_is_app("/private/var/containers/Bundle/Application/1234-ABCD/SubwaySurf.app/SubwaySurf"), "private app path");
	CHECK(n2mg_path_is_app("/var/containers/Bundle/Application/X/Y.app/Y"), "var app path");
	CHECK(!n2mg_path_is_app("/Applications/Preferences.app/Preferences"), "system app protected");
	CHECK(!n2mg_path_is_app("/usr/libexec/trustd") && !n2mg_path_is_app("/usr/local/bin/n2sample") && !n2mg_path_is_app(NULL) && !n2mg_path_is_app(""), "daemons");
	CHECK(!n2mg_path_is_app("/private/var/containers/Bundle/Applicatio"), "prefix only");
	struct n2mg_cand c = { .pid = 500, .band = 0, .memstat_state = 0, .system_or_exiting = false, .footprint = 1, .name = "SubwaySurf" };
	CHECK(n2mg_verdict_class(&c, 30, true) == N2MG_OK, "app killable");
	CHECK(n2mg_verdict_class(&c, 30, false) == N2MG_NO_CLASS, "non-app protected");
	CHECK(n2mg_verdict(&c, 30) == N2MG_OK, "0420 verdict unchanged");
	const char *prot[] = { "vcodecd", "n2sample", "amfid", "trustd", "CommCenterMobileHelper", "CommCenterRootHelper", "ProtectedCloudKe",
		"com.apple.WebKit.GPU", "suggestd", "triald", "Preferences", "Camera", "MobileSlideShow", "PosterBoard", "SpringBoard" };
	for (unsigned i = 0; i < sizeof(prot) / sizeof(prot[0]); i++) {
		c.name = prot[i];
		CHECK(n2mg_verdict_class(&c, 30, true) == N2MG_NO_NAME, prot[i]);
	}
	c.name = "SubwaySurf"; CHECK(!n2mg_name_protected("SubwaySurf"), "third-party app name not listed");
	/* budget: picks every 2 s under constant pressure */
	struct n2mg_budget b; memset(&b, 0, sizeof(b));
	unsigned picks = 0, pauses = 0; uint64_t first_after_pause = 0;
	for (uint64_t t = 90000; t < 90000 + 400000; t += 2000) {
		bool p = false;
		if (n2mg_budget_ok(&b, t, &p)) { n2mg_budget_note(&b, t); picks++; if (pauses && !first_after_pause) first_after_pause = t; }
		if (p) pauses++;
	}
	/* 400 s: 3 picks (6 s), pause 120 s from the 4th attempt, then 3 again ... */
	CHECK(pauses >= 2 && pauses <= 4, "pauses");
	CHECK(picks <= 3 * (pauses + 1), "at most 3 per window+pause");
	CHECK(first_after_pause >= 90000 + 6000 + 120000, "nothing during the 120 s pause");
	/* sparse picks (one per 30 s) never pause */
	memset(&b, 0, sizeof(b)); pauses = 0;
	for (uint64_t t = 1000; t < 1000 + 600000; t += 30000) { bool p = false; if (n2mg_budget_ok(&b, t, &p)) n2mg_budget_note(&b, t); if (p) pauses++; }
	CHECK(pauses == 0, "one per 30 s: within the budget");
	printf("%s: %d failures (picks %u)\n", fails ? "FAILED" : "ALL OK", fails, picks); return fails != 0;
}
