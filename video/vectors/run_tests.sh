#!/bin/bash
# SPDX-License-Identifier: MIT
# Package entry point required by the common rules; check_vectors.sh does the work.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$here/check_vectors.sh" "$@"
