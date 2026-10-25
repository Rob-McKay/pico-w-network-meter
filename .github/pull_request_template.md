## What and why

<!-- What the change does, and the problem or request behind it. -->

## Testing

<!-- What was run, and on what. Say what was NOT tested (for example, on a real Pico W). -->

- [ ] `make test` passes (docs check, style check, unit and screen tests)
- [ ] `make pico` builds with no warnings from this repository's code
- [ ] Tested on a Pico W with a Display Pack 2.0: <!-- what was checked, or "not tested on hardware" -->
- [ ] `make image` needed after merging? <!-- yes (Dockerfile, west.yml or zephyr/patches changed) / no -->

## Review checklist

Rules from [docs/CODING.md](../docs/CODING.md). Ticked by the reviewer; anything
not met is explained in a review comment.

### Function documentation (`scripts/check-docs.py`)
- [ ] Every function has a Doxygen block with `@brief`, an `@param` for every
      parameter (`[out]` / `[in,out]` for pointers written through), and `@return`
      or `@retval` for every non-`void` function
- [ ] Every function has `@sideeffects` listing **all** global state it changes:
      statics and globals written (and the lock held), hardware driven, queues,
      semaphores and work items, threads started, woken or blocked, log
      messages, files, and, in C, what happens to `errno` (even "errno unchanged")
- [ ] The documented side effects match the code, including those of the
      functions it calls
- [ ] Public functions: the interface is documented in the header only; the
      definition has a plain comment, "Public interface: documented in
      `<header>.h`.", plus any private implementation details worth
      explaining. `static` functions and tests have their doc block at the
      definition

### Single responsibility
- [ ] Each function does one job at one level of abstraction; no "and" in its
      `@brief`
- [ ] Hardware-independent logic stays out of driver/UI code, so it can be unit
      tested on native_sim

### Undefined and implementation-defined behaviour
- [ ] No signed overflow, shifts by negative or oversized amounts, or shifts of
      negative values; worst-case values checked for arithmetic
- [ ] Narrowing casts and conversions to signed types only of values that fit
- [ ] Array indexes and buffer sizes bounded; no unbounded string functions
      (`strcpy`, `sprintf`, ...)
- [ ] Format specifiers match their argument types after promotion (cast
      `uint8_t` to `unsigned int` for `%u`)
- [ ] No reads of uninitialised memory, padding (no `memcmp` of structs) or
      NULL; division by zero impossible
- [ ] No reliance on implementation-defined behaviour (`char` signedness,
      right shift of negatives, enum sizes) without a comment saying why it's safe

### Concurrency
- [ ] Every variable shared between threads (main, scan, net_mgmt events,
      input, work queues) is accessed only under its documented lock, or is an
      atomic
- [ ] No lock held while blocking on another thread; lock order consistent
- [ ] Callbacks from other threads (net_mgmt, input, work items) are short and
      never block indefinitely
- [ ] Code that must run on one thread (display, LED) says so

### Zephyr standards
- [ ] `scripts/check-style.sh` clean (Zephyr's `.clang-format` and `checkpatch.pl`)
- [ ] Zephyr's driver models and APIs used (devicetree, LED, input, display,
      net_mgmt) rather than direct register access; Kconfig options prefixed
      `METER_`
- [ ] Errors returned as negative errno codes and logged with `LOG_*`; `errno`
      not used for the meter's own errors
- [ ] Zephyr macros (`ARRAY_SIZE`, `MIN`, `CLAMP`, `BUILD_ASSERT`, `DT_*`) used
      where they fit

### Housekeeping
- [ ] Every new or changed source file starts with `Copyright (c) <year> Rob McKay`
      and `SPDX-License-Identifier: Apache-2.0`
- [ ] Carried Zephyr patches listed in `zephyr/patches.yml` with their reason
- [ ] README, `docs/USAGE.md` and the quick reference updated if behaviour,
      buttons, LED colours or settings changed
- [ ] New behaviour has tests; screen changes checked in `tests/ui_render`
