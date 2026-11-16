#include <stdio.h>
#include <string.h>
#include <errno.h>
int sweet_sbcont_pick(unsigned int mode, const char *exe, const char *blk, size_t blen, char *out, size_t outlen, const char **why);
static int fails;
static void t(const char *name, unsigned mode, const char *exe, const char *blk, size_t blen, size_t outlen, int want, const char *wantpath) {
	char out[1024] = "?"; const char *why = "";
	int r = sweet_sbcont_pick(mode, exe, blk, blen, out, outlen, &why);
	int ok = r == want && (want != 0 || strcmp(out, wantpath) == 0);
	if (!ok) fails++;
	printf("%s %-28s rc=%d out=%s (%s)\n", ok ? "OK  " : "FAIL", name, r, r == 0 ? out : "-", why);
}
#define B(s) s, sizeof(s)
int main(void) {
	const char *appex = "/System/Library/PrivateFrameworks/DocumentManagerUICore.framework/PlugIns/com.apple.DocumentManager.Service.appex/com.apple.DocumentManager.Service";
	t("appex container HOME", 1, appex, B("/x\0arg\0PATH=/usr/bin\0HOME=/private/var/mobile/Containers/Data/PluginKitPlugin/AAAA\0TMPDIR=/t\0"), 1024, 0, "/private/var/mobile/Containers/Data/PluginKitPlugin/AAAA");
	t("appex /var HOME -> /private", 1, appex, B("HOME=/var/mobile/Containers/Data/PluginKitPlugin/B\0"), 1024, 0, "/private/var/mobile/Containers/Data/PluginKitPlugin/B");
	t("appex no HOME", 1, appex, B("PATH=/usr/bin\0"), 1024, 0, "/private/var/mobile");
	t("appex null block", 1, appex, NULL, 0, 1024, 0, "/private/var/mobile");
	t("appex HOME=/ only uses mobile", 1, appex, B("HOME=/\0"), 1024, 0, "/private/var/mobile");
	t("app mode1 refused", 1, "/private/var/containers/Bundle/Application/U/TrollStore.app/TrollStore", B("HOME=/private/var/mobile/Containers/Data/Application/U\0"), 1024, ENOENT, "");
	t("app mode2 container", 2, "/private/var/containers/Bundle/Application/U/X.app/X", B("HOME=/private/var/mobile/Containers/Data/Application/U\0"), 1024, 0, "/private/var/mobile/Containers/Data/Application/U");
	t("daemon mode2 no container", 2, "/usr/libexec/foo", B("HOME=/var/root\0"), 1024, ENOENT, "");
	t("mode0", 0, appex, B("HOME=/var/mobile/Containers/x\0"), 1024, ENOENT, "");
	t("small buffer", 1, appex, B("HOME=/private/var/mobile/Containers/Data/PluginKitPlugin/AAAA\0"), 10, ERANGE, "");
	t("unterminated block", 1, appex, "HOME=/var/mobile/Containers/Q", 29, 1024, 0, "/private/var/mobile/Containers/Q");
	t("last HOME wins", 1, appex, B("HOME=/a\0HOME=/var/mobile/Containers/Z\0"), 1024, 0, "/private/var/mobile/Containers/Z");
	printf("%s\n", fails ? "T_PICK FAILED" : "T_PICK ALL OK"); return fails != 0;
}
