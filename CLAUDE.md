# Working on this repository

Pico W Wi-Fi channel meter: a Zephyr application for a Raspberry Pi Pico W
with a Pimoroni Pico Display Pack 2.0, built in Docker with Arm Toolchain
for Embedded (clang). See README.md for the project and docs/USAGE.md for
using the device.

Every change follows the rules in [docs/CODING.md](docs/CODING.md) and is
reviewed against the checklist in
[.github/pull_request_template.md](.github/pull_request_template.md). In
short:

## Function documentation

- Every function (C, Python, CMake) has a Doxygen-style block with
  `@brief`, `@param` for every parameter (`[out]` / `[in,out]` for pointers
  written through), `@return` or `@retval` unless it returns nothing, and
  `@sideeffects`.
- `@sideeffects` lists every global state change: statics and globals
  written (and the lock held), hardware driven, queues, semaphores and work
  items, threads started, woken or blocked, log messages, files, and the
  effects of the functions it calls. In C it always says what happens to
  `errno`, even "errno unchanged". The meter's functions return negative
  errno codes and never set `errno`; `snprintf()` may set it.
- Public functions: the interface is documented in the header only. The
  definition in the `.c` file has a plain `/*` comment, "Public interface:
  documented in `<header>.h`.", plus any private implementation details
  worth explaining. Never repeat the header's doc block there.
- `static` functions and `ZTEST` cases: the full doc block at the definition.

## Code

- Single responsibility: one job per function, at one level of abstraction.
  Keep hardware-independent logic (like `chan_stats.c`, `ap_history.c`)
  free of Zephyr device APIs, so it is unit tested on native_sim.
- No undefined or implementation-defined behaviour: check worst-case values
  for overflow and shifts, cast narrowing conversions only when the value
  fits, and match format specifiers after promotion (cast `uint8_t` to
  `unsigned int` for `%u`). Never `memcmp()` structs (padding); compare
  field by field. No NULL dereference or division by zero, even on misuse.
- Concurrency: shared data only under its documented mutex (or atomics);
  callbacks from other threads short and non-blocking; display and LED code
  on the main thread only.
- Zephyr standards: Zephyr's `.clang-format` and `checkpatch.pl`
  (`scripts/check-style.sh`; `--fix` reformats), Zephyr driver models and
  APIs, negative errno return codes, `LOG_*`, `ARRAY_SIZE`/`MIN`/
  `BUILD_ASSERT`, Kconfig options prefixed `METER_`. Warnings are errors and
  clang-tidy runs on every file.
- No unbounded string functions (`strcpy`, `sprintf`, ...).

## Files

- Every source file starts with `Copyright (c) <year> Rob McKay` and
  `SPDX-License-Identifier: Apache-2.0` (C: in the opening `/* */` block;
  others: two `#` lines, after any `#!`). The licence is Apache 2.0
  (LICENSE).
- Patches to Zephyr are carried in `zephyr/patches/` and listed with their
  reason in `zephyr/patches.yml`; they are applied when the image is built.
- When behaviour, buttons, LED colours or settings change, update README.md,
  docs/USAGE.md and docs/quick-reference.html (and its PDF).

## Checking a change

- `make test` must pass: the documentation check (`scripts/check-docs.py`),
  the style check, and the native_sim tests under AddressSanitizer. Add tests
  for new behaviour; screen changes go through `tests/ui_render`.
- `make pico` must build with no warnings from this repository's code.
- If the Dockerfile, west.yml or zephyr/patches change, `make image` first.
- Say plainly what was and wasn't tested on a real Pico W.

## Git

- Work on a branch and open a pull request for review; don't push to `main`.
- Commit messages explain why, not only what.
