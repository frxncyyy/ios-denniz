#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Print the exported differential kernel names, one per line, sorted.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
grep -hE '^uint32_t[[:space:]]+k[0-9a-z_]+[[:space:]]*\(' "$here"/kernels/*.c \
	| sed -E 's/^uint32_t[[:space:]]+(k[0-9a-z_]+).*/\1/' \
	| sort -u
