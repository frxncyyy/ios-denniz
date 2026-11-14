/* SPDX-License-Identifier: MIT */
/*
 * n2ip_deps.h -- the two track-P interfaces vcodecd consumes, in one place.
 *
 *   host/n2vhost.h  (IV)  bring up the phone's vendor codec set under the interpreter
 *   ap/n2ap_venc.h  (V41) the encoder session the Linux V4L2 driver would own
 *
 * Both packages are in the tree now, so this file only picks them up; there is no private copy and no fallback declaration
 * to drift.  Compiling vcodecd needs -I gpu/hwcodec/host and -I gpu/hwcodec/ap (build_ios.sh adds them; daemon/test/
 * run_tests.sh does too).  If either header is missing the build stops here with the reason, not with a wrong guess.
 */
#ifndef N2IP_DEPS_H
#define N2IP_DEPS_H

#if defined(__has_include)
#  if !__has_include("n2vhost.h")
#    error "vcodecd needs gpu/hwcodec/host/n2vhost.h (package IV) on the include path"
#  endif
#  if !__has_include("n2ap_venc.h")
#    error "vcodecd needs gpu/hwcodec/ap/n2ap_venc.h (package V41) on the include path"
#  endif
#endif

#include "n2vhost.h"
#include "n2ap_venc.h"

#endif /* N2IP_DEPS_H */
