#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Check that each of this repository's headers includes what it uses.

clang-tidy's misc-include-cleaner (.clang-tidy) runs on every compile, but
it looks only at the file being compiled: a header that uses uint8_t and
relies on whoever includes it to have included <stdint.h> passes there.
This runs the check on each header as a file of its own, with the compile
flags of a source beside it in the given build directories, and the same
options (.clang-tidy's IgnoreHeaders among them).

    check-header-includes.py <build dir>...

The build directories are native_sim builds with compile_commands.json
(scripts/test.sh passes its test builds). Prints each finding; exits 1 if
there are any, or if a header has no source to borrow flags from.
"""

import glob
import json
import os
import shlex
import subprocess
import sys

## Only the include check: the builds already run the others on headers.
CHECKS = "-*,misc-include-cleaner"


## @brief Find this repository's headers.
#
# @param repo Repository root.
# @return Paths of app/src's headers (subdirectories included) and the test
#         suites' own, sorted.
#
# @sideeffects None (reads the directory tree).
def headers(repo):
    found = glob.glob(os.path.join(repo, "app", "src", "**", "*.h"), recursive=True)
    found += glob.glob(os.path.join(repo, "tests", "*", "src", "*.h"))
    return sorted(found)


## @brief Read the compile commands of this repository's sources.
#
# @param repo Repository root.
# @param build_dirs Build directories holding compile_commands.json.
# @return {source path: compile command entry}, the first build's entry for
#         a source built in several.
#
# @sideeffects None (reads the compile_commands.json files).
def compile_entries(repo, build_dirs):
    entries = {}
    for build_dir in build_dirs:
        with open(os.path.join(build_dir, "compile_commands.json"), encoding="utf-8") as f:
            for e in json.load(f):
                src = os.path.normpath(os.path.join(e["directory"], e["file"]))
                if src.startswith(repo + os.sep) and os.sep + "build" + os.sep not in src:
                    entries.setdefault(src, e)
    return entries


## @brief Pick the compile command to check a header with: its own
#         source's, else that of any source in the same directory.
#
# @param header The header.
# @param entries compile_entries()'s result.
# @return The entry, or None if no source in its directory was built.
#
# @sideeffects None.
def entry_for(header, entries):
    own = entries.get(header[:-2] + ".c")
    if own is not None:
        return own
    here = os.path.dirname(header)
    return next((e for src, e in sorted(entries.items()) if os.path.dirname(src) == here), None)


## @brief Turn a source's compile command into one that compiles a header
#         as C.
#
# @param entry The source's compile command entry.
# @param header The header.
# @return The compiler and its arguments, ending in "-x c -c <header>".
#
# @sideeffects None.
def header_command(entry, header):
    src = os.path.normpath(os.path.join(entry["directory"], entry["file"]))
    args = entry.get("arguments") or shlex.split(entry["command"])
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
        elif a == "-o":
            skip = True
        elif a != "-c" and os.path.normpath(os.path.join(entry["directory"], a)) != src:
            out.append(a)
    return out + ["-Wno-unused-function", "-x", "c", "-c", header]


## @brief Run the include check on one header.
#
# @param repo Repository root.
# @param header The header.
# @param entry The compile command entry to borrow flags from.
# @return clang-tidy's findings, one per line; empty if there are none.
#
# @sideeffects Runs scripts/clang-tidy-cross.py (and so clang-tidy) as a subprocess.
def check(repo, header, entry):
    cmd = [sys.executable, os.path.join(repo, "scripts", "clang-tidy-cross.py"),
           "--quiet", "--checks=" + CHECKS, header, "--"] + header_command(entry, header)
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=entry["directory"])
    lines = [ln for ln in (r.stdout + r.stderr).splitlines()
             if "warning:" in ln or "error:" in ln]
    if r.returncode != 0 and not lines:
        lines = [f"{header}: clang-tidy failed ({r.returncode})"]
    return lines


## @brief Check every header; report and exit.
#
# @sideeffects Prints findings and a summary; exits with status 1 if any header fails or
#              can't be checked, 2 on bad usage.
def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[0], file=sys.stderr)
        sys.exit(2)
    repo = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    entries = compile_entries(repo, sys.argv[1:])
    failed = 0
    all_headers = headers(repo)
    for h in all_headers:
        entry = entry_for(h, entries)
        if entry is None:
            print(f"{os.path.relpath(h, repo)}: no source beside it was built", file=sys.stderr)
            failed += 1
            continue
        found = check(repo, h, entry)
        for line in found:
            print(line.replace(repo + os.sep, ""))
        failed += bool(found)
    if failed:
        print(f"{failed} of {len(all_headers)} headers don't include what they use "
              "(docs/CODING.md)", file=sys.stderr)
        sys.exit(1)
    print(f"All {len(all_headers)} headers include what they use")


if __name__ == "__main__":
    main()
