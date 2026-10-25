#!/usr/bin/env bash
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Check this repository's C against Zephyr's coding style: Zephyr's
# .clang-format (formatting) and checkpatch.pl (the kernel-style rules
# Zephyr applies to its own code). Runs inside the Docker image; prints
# what differs and fails if anything does.
#
#   scripts/check-style.sh          check
#   scripts/check-style.sh --fix    reformat the files in place first
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
zephyr=${ZEPHYR_BASE:-/workdir/zephyr}
mapfile -t files < <(cd "$here" && ls app/src/*.[ch] tests/*/src/*.[ch])
status=0

cd "$here"
if [[ ${1:-} == --fix ]]; then
	clang-format --style="file:$zephyr/.clang-format" -i "${files[@]}"
fi
for f in "${files[@]}"; do
	if ! clang-format --style="file:$zephyr/.clang-format" "$f" | diff -u "$f" - ; then
		status=1
	fi
done
# From Zephyr's tree, so that checkpatch reads Zephyr's .checkpatch.conf:
# the rules as Zephyr applies them to its own code.
for f in "${files[@]}"; do
	if ! (cd "$zephyr" && ./scripts/checkpatch.pl --no-tree --terse --quiet -f "$here/$f"); then
		status=1
	fi
done

if [[ $status -ne 0 ]]; then
	echo "Style check failed: scripts/check-style.sh --fix reformats (docs/CODING.md)"
else
	echo "Style matches Zephyr's (clang-format, checkpatch)"
fi
exit $status
