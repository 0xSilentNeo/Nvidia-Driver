#!/bin/sh
# SPDX-License-Identifier: MIT
# Apply the local patch series in patches/ on top of the vendored upstream
# sources. Patches are plain unified diffs rooted at the repository root.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
for p in patches/*.patch; do
	[ -e "$p" ] || continue
	echo "applying $p"
	patch -p1 --forward --silent < "$p"
done
