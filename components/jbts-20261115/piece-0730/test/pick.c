#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <bsd/string.h>
int
sweet_sbcont_pick(unsigned int mode, const char *exe, const char *blk, size_t blen, char *out, size_t outlen, const char **why)
{
	const char *home = NULL;
	size_t hlen = 0, i = 0;
	int appex = 0, contained = 0;

	if (mode == 0 || outlen < 2) {
		return ENOENT;
	}
	for (const char *s = exe; s != NULL && *s; s++) {
		if (strncmp(s, ".appex/", 7) == 0) {
			appex = 1;
			break;
		}
	}
	while (blk != NULL && i < blen) {               /* NUL-separated strings; the last HOME= wins (env comes after argv) */
		size_t n = strnlen(blk + i, blen - i);
		if (n > 5 && strncmp(blk + i, "HOME=", 5) == 0 && blk[i + 5] == '/') {
			home = blk + i + 5;
			hlen = n - 5;
		}
		i += n + 1;
	}
	if (home != NULL && (strncmp(home, "/private/var/mobile/Containers/", 31) == 0 ||
	    strncmp(home, "/var/mobile/Containers/", 23) == 0)) {
		contained = 1;
	}
	if (!(appex || (mode >= 2 && contained))) {
		return ENOENT;
	}
	if (home != NULL && hlen > 1) {
		const char *pre = strncmp(home, "/var/", 5) == 0 ? "/private" : "";
		size_t plen = strlen(pre);
		if (plen + hlen + 1 > outlen) {
			return ERANGE;
		}
		memcpy(out, pre, plen);
		memcpy(out + plen, home, hlen);
		out[plen + hlen] = '\0';
		*why = contained ? "HOME container" : "HOME";
		return 0;
	}
	if (outlen < sizeof("/private/var/mobile")) {
		return ERANGE;
	}
	strlcpy(out, "/private/var/mobile", outlen);
	*why = "no HOME, mobile home";
	return 0;
}

