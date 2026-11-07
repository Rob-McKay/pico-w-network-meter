#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Check that each C function is followed by at least two blank lines.

clang-format (.clang-format) keeps up to two blank lines, but has no option
that requires two. This does:
after a function's closing brace (a line holding only "}"), whatever comes
next in the file has at least two blank lines before it. The end of the file
needs none, and an #else, #elif or #endif straight after the function is
skipped over, so the code after it needs the two blank lines instead.

    check-blank-lines.py [--fix] <file>...

Prints each place that has fewer, as "<file>:<line>: ..."; exits 1 if there
are any. With --fix, adds the missing blank lines instead, and exits 0.
"""

import sys

## The preprocessor lines that close a conditional, skipped after a function.
CONDITIONAL_ENDS = ("#else", "#elif", "#endif")
## The blank lines wanted after a function.
WANTED = 2


## @brief Tell whether a line closes a function: "}" alone, in column 0.
#
# @param line The line, without its newline.
# @return True if it does.
#
# @sideeffects None.
def closes_function(line):
    return line.rstrip() == "}"


## @brief Tell whether a line closes a preprocessor conditional.
#
# @param line The line, without its newline.
# @return True for #else, #elif and #endif, with or without spaces after
#         the #.
#
# @sideeffects None.
def closes_conditional(line):
    words = line.strip().replace("# ", "#", 1).split()
    return bool(words) and words[0] in CONDITIONAL_ENDS


## @brief Find what follows a function, and the blank lines just before it.
#
# @param lines The file's lines, without newlines.
# @param end The index of the function's closing brace.
# @return (index of the next line that is neither blank nor a conditional's
#         end, blank lines immediately before it), or None at the end of the
#         file.
#
# @sideeffects None.
def next_after(lines, end):
    blanks = 0
    for i in range(end + 1, len(lines)):
        if not lines[i].strip():
            blanks += 1
        elif closes_conditional(lines[i]):
            blanks = 0
        else:
            return i, blanks
    return None


## @brief Find the places in a file where a function is followed by too few
#         blank lines.
#
# @param lines The file's lines, without newlines.
# @return [(index of the line that needs them, blank lines it has)], in order.
#
# @sideeffects None.
def find_short(lines):
    short = []
    for i, line in enumerate(lines):
        if closes_function(line):
            found = next_after(lines, i)
            if found is not None and found[1] < WANTED:
                short.append(found)
    return short


## @brief Add the missing blank lines to a file's lines.
#
# @param lines The file's lines, without newlines.
# @param short The places that need them, from find_short().
# @return The lines with WANTED blank lines before each place.
#
# @sideeffects None.
def fixed(lines, short):
    out = list(lines)
    for index, blanks in reversed(short):
        out[index:index] = [""] * (WANTED - blanks)
    return out


## @brief Check, or fix, one file.
#
# @param path The file.
# @param fix True to add the missing blank lines.
# @return The problems found, as "<file>:<line>: ..." messages; empty if none,
#         or if they were fixed.
#
# @sideeffects Reads the file; with @p fix, rewrites it if it needed lines.
def check_file(path, fix):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    lines = text.split("\n")
    short = find_short(lines)
    if not short:
        return []
    if fix:
        with open(path, "w", encoding="utf-8") as f:
            f.write("\n".join(fixed(lines, short)))
        return []
    return [f"{path}:{index + 1}: {blanks} blank line(s) after a function, "
            f"{WANTED} wanted" for index, blanks in short]


## @brief Check, or fix, the files named on the command line.
#
# @param argv The arguments: an optional --fix, then the files.
# @retval 0 Every file has the blank lines (or --fix added them).
# @retval 1 Some don't (printed), or no files were named.
#
# @sideeffects Reads the files, and with --fix rewrites those that need it;
#              prints each problem to stdout, and usage to stderr.
def main(argv):
    fix = bool(argv) and argv[0] == "--fix"
    paths = argv[1:] if fix else argv
    if not paths:
        print(__doc__.strip(), file=sys.stderr)
        return 1
    problems = []
    for path in paths:
        problems += check_file(path, fix)
    for problem in problems:
        print(problem)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
