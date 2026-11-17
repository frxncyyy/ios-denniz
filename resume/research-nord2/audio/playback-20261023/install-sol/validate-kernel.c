/* SPDX-License-Identifier: MIT
 * ADT validator only. This deliberately performs NO filesystem mutation.
 * Caller must run before launchd, after root is mounted. Not an installer.
 */
#include <sys/types.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
#include <pexpert/device_tree.h>
#include <libkern/crypto/sha2.h>
#include "package.h"
static int n2ap_kernel_hash(const unsigned char *p, unsigned n,
    const unsigned char expected[32])
{
    unsigned char actual[32]; unsigned diff=0, i;
    SHA256_CTX ctx;
    SHA256_Init(&ctx); SHA256_Update(&ctx,p,n); SHA256_Final(actual,&ctx);
    for(i=0;i<32;i++) diff |= actual[i]^expected[i];
    return diff ? EINVAL : 0;
}
int nord2_audio_package_validate(struct n2ap_view *out)
{
    DTEntry chosen;
    const void *bytes=NULL;
    unsigned size=0;
    int mode=0;
    (void)PE_parse_boot_argn("nord2audioinstall",&mode,sizeof(mode));
    if (!mode) return ENOENT;
    /* mode 2 is reserved for rollback and must not require an ADT package. */
    if (mode!=1) return ENOTSUP;
    if(SecureDTLookupEntry(NULL,"/chosen",&chosen)!=kSuccess ||
       SecureDTGetProperty(chosen,"nord2-audio-package",&bytes,&size)!=kSuccess)
        return ENOENT;
    return n2ap_parse(bytes,size,n2ap_kernel_hash,out) ? EINVAL : 0;
}
