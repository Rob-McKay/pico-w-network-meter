#!/usr/bin/env bash
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0
#
# Check this repository's C against Zephyr's coding style: the repository's
# .clang-format (Zephyr's, plus two blank lines kept between functions),
# checkpatch.pl (the kernel-style rules Zephyr applies to its own code), and
# scripts/check-blank-lines.py (two blank lines after each function, which
# clang-format can't require). Also checks that .clang-format's copy of
# Zephyr's file still matches the pinned Zephyr's. Runs inside the Docker
# image; prints what differs and fails if anything does.
#
#   scripts/check-style.sh          check
#   scripts/check-style.sh --fix    reformat the files in place first
set -euo pipefail

here=$(cd "$(dirname "$0")/.." && pwd)
zephyr=${ZEPHYR_BASE:-/workdir/zephyr}
mapfile -t files < <(cd "$here" && find app/src tests/*/src -name '*.[ch]' | sort)
status=0

cd "$here"
# .clang-format holds Zephyr's file between two marker lines.
if ! sed -n "/^# ---- Zephyr's .clang-format, unchanged ----$/,/^# ---- End of Zephyr's .clang-format ----$/p" \
	.clang-format | sed '1d;$d' | diff -u "$zephyr/.clang-format" - ; then
	echo ".clang-format: its copy of Zephyr's .clang-format is out of date: copy it again"
	status=1
fi
if [[ ${1:-} == --fix ]]; then
	clang-format --style=file:.clang-format -i "${files[@]}"
	"$here/scripts/check-blank-lines.py" --fix "${files[@]}"
fi
for f in "${files[@]}"; do
	if ! clang-format --style=file:.clang-format "$f" | diff -u "$f" - ; then
		status=1
	fi
done
if ! "$here/scripts/check-blank-lines.py" "${files[@]}"; then
	status=1
fi
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
