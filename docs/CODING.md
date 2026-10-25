# Coding conventions

These follow [pi-zephyr-inet-radio](https://github.com/Rob-McKay/pi-zephyr-inet-radio)'s
conventions, cut down to what this project needs. They apply to the
repository's own C and Python. `make test` enforces what a tool can check:

| Rule | Checked by |
|---|---|
| 0. No compiler warnings, clean clang-tidy | the build itself (`cmake/strict.cmake`) |
| 1. Doxygen comment on every function | `scripts/check-docs.py` |
| 2. Single responsibility | review |
| 3. No unbounded string functions; buffer overflows trapped | clang-tidy, and `_FORTIFY_SOURCE=2` in the firmware |
| 4. Zephyr's driver models first | review |

## 0. The build checks every file

Every target with our code calls `meter_strict_warnings()` from
`cmake/strict.cmake`, which:

- compiles with `-Wall -Wextra -Wpedantic -Werror`: any warning fails the
  build;
- runs clang-tidy (`.clang-tidy`, the same checks as the radio's) on each
  source as it compiles, all findings as errors. clang-tidy 20 or later
  must be installed (the Docker image has 23.1.3 from PyPI).
  `-DMETER_CLANG_TIDY=OFF` skips it for a quick local build only.

Zephyr's headers are included as system headers, so only this
repository's code is checked. `scripts/clang-tidy-cross.py` gives
clang-tidy the C library headers of the ATfE cross compile.

When a finding is a deliberate choice, suppress that one line with a
reason, never the whole check. The `#pragma GCC diagnostic` blocks around
Zephyr's file-scope `K_*_DEFINE` and `DEVICE_DT_INST_DEFINE` macros are
such a case: their expansion can end in an empty statement, which
`-Wpedantic` reports.

## 1. Every function has a Doxygen comment

- `@brief`: one line saying what the function does;
- `@param` for every parameter (`@param[out]`, `@param[in,out]` for
  pointers written through);
- `@return` or `@retval` lines, except for `void` functions.

Public functions are documented at their declaration in the header,
`static` ones at their definition, test cases with a one-line `@brief`.

## 2. Single responsibility

A function does one job at one level of abstraction. In particular,
hardware-independent logic (`chan_stats.c`, `text_raster.c`) stays free of
Zephyr device APIs so it can be unit tested on native_sim. The screens are
tested on a fake panel (`tests/ui_render`).

## 3. Strings and buffers

`strcpy`, `strcat`, `sprintf`, `vsprintf` and `gets` are banned (clang-tidy).
Use `snprintf`, or `memcpy` with an explicit length and terminator. The
firmware is built with `_FORTIFY_SOURCE=2`, so an overflow in a C library
call stops it instead of corrupting memory.

## 4. Zephyr's driver models first

The Display Pack is described in devicetree and used through Zephyr's
drivers: the ST7789V display driver, `gpio-keys` with the input subsystem,
and `gpio-leds` with the LED API. The Wi-Fi chip is used through Zephyr's
AIROC driver and Wi-Fi management API. Write a driver only when Zephyr has
none.
