#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Run clang-tidy on a source file using the build's compile command.

CMake's <LANG>_CLANG_TIDY hook (see cmake/strict.cmake) runs this before
each compile as

    clang-tidy-cross.py [--clang-tidy=<exe>] [clang-tidy options] <source> -- <compiler> <args>

<exe> is the clang-tidy to run (cmake/strict.cmake passes the one it
checked); without it, the first clang-tidy on PATH.

The builds use three compilers (cmake/toolchain.cmake): the Pico firmware
Arm Toolchain for Embedded (ATfE), a clang cross compiler that finds its C
library's headers in its per-target runtimes; the native_sim tests the
host's clang; and the ESP32-C5 firmware the Zephyr SDK's GCC. clang-tidy's
own clang knows neither ATfE's runtimes and configuration files nor GCC's
target and C library, so for a cross compile this wrapper adds the C
library's include directories as the real compiler finds them. For GCC it
also gives clang the target GCC compiles for and leaves out the options
clang doesn't know. Then it runs clang-tidy and returns its exit status.
Findings are reported in this repository's headers as well as its sources.
"""

import os
import re
import shutil
import subprocess
import sys

## A compiler's own built-in headers (stddef.h, ...), clang's or GCC's:
#  clang-tidy's clang has its own.
BUILTIN_HEADERS = re.compile(r"/lib/(clang/[^/]+|gcc/[^/]+/[^/]+)/include(-fixed)?$")

## GCC options clang rejects or would misread, dropped from a GCC compile
#  command: exact names, then prefixes (--param=..., -specs=...).
GCC_ONLY = {"-fno-defer-pop", "-fno-reorder-functions", "-fstrict-volatile-bitfields",
            "-fno-printf-return-value"}
GCC_ONLY_PREFIXES = ("--param=", "-specs=", "--specs=", "--sysroot")

## A command-line macro defined as a negative number (-DNAME=-1).
NEGATIVE_DEFINE = re.compile(r"^(-D[A-Za-z_][A-Za-z0-9_]*)=(-[0-9][0-9A-Fa-fxXuUlL]*)$")

## The options that choose a GCC's target and C library, kept when asking it
#  for its include directories.
GCC_HEADER_OPTS = ("--sysroot", "-specs=", "--specs=", "-march=", "-mabi=")


## @brief Ask a compiler for its system include directories.
#
# The compiler's own built-in headers are left out; what remains is the C
# library's.
#
# @param compiler_cmd The compiler and the options that select headers
#        (--sysroot, --target=, --config=, -m...).
# @return Directories, in search order.
#
# @sideeffects Runs the compiler as a subprocess (preprocessing an empty input).
def compiler_system_includes(compiler_cmd):
    out = subprocess.run(compiler_cmd + ["-E", "-Wp,-v", "-x", "c", os.devnull],
                         capture_output=True, text=True).stderr
    dirs, inside = [], False
    for line in out.splitlines():
        if line.startswith("#include <...>"):
            inside = True
        elif line.startswith("End of search list"):
            break
        elif inside:
            d = os.path.normpath(line.strip())
            if not BUILTIN_HEADERS.search(d):
                dirs.append(d)
    return dirs


## @brief Tell whether a compiler is GCC, by its name.
#
# @param compiler Path of the compiler.
# @return True for a GCC driver (riscv64-zephyr-elf-gcc, gcc, ...).
#
# @sideeffects None.
def is_gcc(compiler):
    return re.search(r"(^|-)gcc(-[0-9.]+)?$", os.path.basename(compiler)) is not None


## @brief Name the target triple a GCC RISC-V compile command is for.
#
# Zephyr's riscv64-zephyr-elf-gcc builds 32-bit code when -march says rv32.
#
# @param args The compile command's arguments.
# @return A --target= option, or None if the command has no RISC-V -march.
#
# @sideeffects None.
def gcc_target(args):
    for a in args:
        m = re.match(r"-march=rv(32|64)", a)
        if m:
            return f"--target=riscv{m.group(1)}-unknown-elf"
    return None


## @brief Put a negative command-line macro's value in parentheses.
#
# hal_espressif defines CONFIG_ESP_CONSOLE_ROM_SERIAL_PORT_NUM=-1 on every
# compile command, and clang-tidy reports it (bugprone-macro-parentheses)
# at "<command line>", which --header-filter can't exclude. (-1) means the
# same wherever -1 does, in C and in #if.
#
# @param arg One argument of the compile command.
# @return The argument, with the value of a -DNAME=-<number> in parentheses.
#
# @sideeffects None.
def parenthesise_negative_define(arg):
    return NEGATIVE_DEFINE.sub(r"\1=(\2)", arg)


## @brief Translate a GCC cross compile command into one for clang.
#
# @param compiler The GCC.
# @param args Its arguments.
# @return The command for clang-tidy: clang, the target, GCC's C library
#         headers (with clang's own built-in headers in place of GCC's),
#         and the arguments clang understands.
#
# @sideeffects Runs the compiler as a subprocess (compiler_system_includes());
#              exits with a message if the command has no RISC-V target.
def gcc_clang_command(compiler, args):
    target = gcc_target(args)
    if target is None:
        sys.exit(f"clang-tidy-cross.py: no RISC-V -march in a GCC command ({compiler})")
    header_opts = [a for a in args if a.startswith(GCC_HEADER_OPTS)]
    # --sysroot and -specs= are GCC's way to its C library; -nostdlibinc
    # and the -isystem directories below are clang's.
    out = ["clang", "-Wno-unknown-warning-option", target, "-nostdlibinc"]
    out += [parenthesise_negative_define(a) for a in args
            if a not in GCC_ONLY and not a.startswith(GCC_ONLY_PREFIXES)]
    # Zephyr gives a clang build -Wno-gnu (cmake/compiler/clang/
    # compiler_flags.cmake) but GCC no equivalent: its macros (LOG_*) use
    # __auto_type and statement expressions, which clang would otherwise
    # report in this repository's code. The Pico build has the flag already.
    out += ["-Wno-variadic-macro-arguments-omitted", "-Wno-gnu"]
    for d in compiler_system_includes([compiler] + header_opts):
        out += ["-isystem", d]
    return out


## @brief Translate a compile command into one for clang-tidy's clang.
#
# @param cmd The compile command: compiler first.
# @return The command for clang-tidy, after its "--".
#
# @sideeffects For a cross compile, runs the compiler as a subprocess
#              (compiler_system_includes()); for a GCC command without a
#              RISC-V target, exits (gcc_clang_command()).
def clang_command(cmd):
    compiler, args = cmd[0], cmd[1:]
    if is_gcc(compiler):
        return gcc_clang_command(compiler, args)
    # Zephyr's ztest macros (zassert_true(cond), ...) are called without
    # their optional message, which clang 19 and later report under
    # -Wpedantic before C23; the host clang 18 that builds the tests does not.
    out = ["clang", "-Wno-unknown-warning-option"] + args + \
        ["-Wno-variadic-macro-arguments-omitted"]
    if not any(a.startswith("--target=") for a in args):
        return out
    header_opts = [a for a in args if a.startswith(("--sysroot", "--target=", "--config", "-m"))]
    for d in compiler_system_includes([compiler] + header_opts):
        out += ["-isystem", d]
    return out


## @brief Entry point.
#
# @return clang-tidy's exit status.
#
# @sideeffects Runs clang-tidy, which prints its findings, and the compiler (clang_command()).
def main():
    argv = sys.argv[1:]
    if "--" not in argv:
        print("usage: clang-tidy-cross.py [--clang-tidy=<exe>] [clang-tidy args] <source> -- "
              "<compile command>",
              file=sys.stderr)
        return 2
    tidy = shutil.which("clang-tidy")
    if argv[0].startswith("--clang-tidy="):
        tidy = argv.pop(0).split("=", 1)[1]
    sep = argv.index("--")
    # Report findings in this repository's headers, not in other modules'.
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    header_filter = f"--header-filter=^{re.escape(root)}/(app|drivers|tests)/"
    cmd = [tidy, header_filter] + argv[:sep] + ["--"] + clang_command(argv[sep + 1:])
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    sys.exit(main())
