# SPDX-License-Identifier: Apache-2.0
#
# Checks for this repository's own code (docs/CODING.md):
#   - all of -Wall, -Wextra and -Wpedantic, and any warning fails the build;
#   - clang-tidy (.clang-tidy) runs on every source as it compiles, and any
#     finding fails the build.
# Third-party and upstream code keeps its own flags and is not linted.

set(METER_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)

option(METER_CLANG_TIDY "Run clang-tidy on this repository's code as it compiles" ON)

# .clang-tidy's banned-function list (bugprone-unsafe-functions.CustomFunctions)
# needs clang-tidy 20 or later. The Docker image has 23.1.3 from PyPI.
set(METER_CLANG_TIDY_MIN_VERSION 20)

# find_program() validator: accept a clang-tidy new enough for .clang-tidy.
#
# result: set to FALSE in the caller's scope if exe is too old.
# exe: candidate clang-tidy.
function(meter_clang_tidy_new_enough result exe)
  execute_process(COMMAND ${exe} --version OUTPUT_VARIABLE text ERROR_QUIET)
  string(REGEX MATCH "version ([0-9]+)" version "${text}")
  if(NOT CMAKE_MATCH_1 OR CMAKE_MATCH_1 LESS METER_CLANG_TIDY_MIN_VERSION)
    set(${result} FALSE PARENT_SCOPE)
  endif()
endfunction()

if(METER_CLANG_TIDY)
  # A cached clang-tidy from an older checkout may be too old: search again.
  if(METER_CLANG_TIDY_EXE)
    set(ok TRUE)
    meter_clang_tidy_new_enough(ok ${METER_CLANG_TIDY_EXE})
    if(NOT ok)
      unset(METER_CLANG_TIDY_EXE CACHE)
    endif()
  endif()
  find_program(METER_CLANG_TIDY_EXE NAMES clang-tidy VALIDATOR meter_clang_tidy_new_enough)
  if(NOT METER_CLANG_TIDY_EXE)
    message(FATAL_ERROR "clang-tidy ${METER_CLANG_TIDY_MIN_VERSION} or later not found: "
      "pip install clang-tidy==23.1.3, or configure with -DMETER_CLANG_TIDY=OFF")
  endif()
endif()

# Run-time buffer overflow checks in the C library's memory and string
# functions (CONFIG_FORTIFY_SOURCE_RUN_TIME, which cmake/atfe.conf selects).
# Zephyr gives GCC _FORTIFY_SOURCE=2 but nothing for clang, although clang
# and picolibc support it; a failed check calls Zephyr's __chk_fail(), a
# fatal error.
if(CONFIG_FORTIFY_SOURCE_RUN_TIME AND CMAKE_C_COMPILER_ID STREQUAL "Clang")
  set_compiler_property(PROPERTY security_fortify_run_time _FORTIFY_SOURCE=2)
endif()

# Apply the checks to every source of a target.
#
# Zephyr's headers and macros use GNU extensions (statement expressions,
# zero-length arrays, empty variadic macro arguments), so they are included
# as system headers: -Wpedantic and clang-tidy then report only this
# repository's code.
#
# clang-tidy runs through scripts/clang-tidy-cross.py, which gives
# clang-tidy's clang the C library headers of a cross compile (ATfE).
function(meter_strict_warnings target)
  target_compile_options(${target} PRIVATE ${METER_WARNING_FLAGS})
  get_target_property(zephyr_dirs zephyr_interface INTERFACE_INCLUDE_DIRECTORIES)
  target_include_directories(${target} SYSTEM PRIVATE ${zephyr_dirs})
  if(METER_CLANG_TIDY)
    set_target_properties(${target} PROPERTIES C_CLANG_TIDY
      "${PYTHON_EXECUTABLE};${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../scripts/clang-tidy-cross.py;--clang-tidy=${METER_CLANG_TIDY_EXE};--quiet")
  endif()
endfunction()
