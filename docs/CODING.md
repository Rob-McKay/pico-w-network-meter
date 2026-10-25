# Coding conventions

These follow [pi-zephyr-inet-radio](https://github.com/Rob-McKay/pi-zephyr-inet-radio)'s
conventions, cut down to what this project needs. They apply to the
repository's own C and Python. `make test` enforces what a tool can check:

| Rule | Checked by |
|---|---|
| 0. No compiler warnings, clean clang-tidy | the build itself (`cmake/strict.cmake`) |
| 0. Zephyr's coding style | `scripts/check-style.sh`: Zephyr's `.clang-format` and `checkpatch.pl` |
| 1. Doxygen comment on every function: parameters, return values, side effects (errno included) | `scripts/check-docs.py` |
| 2. Single responsibility | review |
| 3. No unbounded string functions; buffer overflows trapped | clang-tidy, and `_FORTIFY_SOURCE=2` in the firmware |
| 4. Zephyr's driver models first | review |

Pull requests are reviewed against these rules with the checklist in
[.github/pull_request_template.md](../.github/pull_request_template.md),
which GitHub fills into every new pull request.
[CLAUDE.md](../CLAUDE.md) summarises them for Claude Code, which reads it
in every session on this repository.

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

Formatting is Zephyr's: tabs, 100 columns, kernel brace style, as its
`.clang-format` lays out. `scripts/check-style.sh --fix` (in `make shell`)
reformats. Where clang-format would wrap a line that must stay whole, such
as a `NOLINTNEXTLINE`, fence it with `/* clang-format off */` and
`/* clang-format on */`.

## Concurrency

Three threads share data: main (display, buttons, LED), the scan thread
(`scan_task.c`) and the net_mgmt event thread (`scanner.c`'s callbacks).
Each shared variable has one mutex, named next to it, held for every
access; nothing is shared through plain globals. The display code
(`gfx.c`, `ui.c`) and the LED cache (`status_led.c`) are main-thread only
and say so. Struct copies are compared field by field (`chan_stats_equal()`),
never with `memcmp()`, which would also compare padding.

## 1. Every function has a Doxygen comment

- `@brief`: one line saying what the function does;
- `@param` for every parameter (`@param[out]`, `@param[in,out]` for
  pointers written through);
- `@return` or `@retval` lines, except for `void` functions;
- `@sideeffects`: every change the function makes to state outside its
  own locals and the objects its pointer parameters point to. That is:
  static and global variables written, with the lock held while doing so;
  hardware driven (the panel, LEDs, radio); queues, semaphores and work
  items given, taken or scheduled; threads started, woken or blocked;
  log messages; files written; and what happens to `errno`. Write
  "None (writes only through @p out)" or "None" when there are none.
  Effects of the functions it calls count too: name them ("Those of
  set_busy() and publish()") rather than leaving them out.

  In C the paragraph always says what happens to `errno`, even if only
  "errno unchanged". The meter's own functions never set `errno`: like
  Zephyr's, they return a negative errno code instead. Among the C library
  calls they make, only `snprintf()` can set it (on an output error).
  Python functions list their effects without `errno`: printing, files,
  subprocesses, exiting.

```c
/**
 * @brief Make a finished scan visible to the display thread.
 *
 * @param ret The scan's outcome (run_scan()).
 * @return The scan's number.
 *
 * @sideeffects Under @c lock: removes faded-out access points from
 *              @c history, merges @c scanning into it, and updates
 *              @c progress. errno unchanged.
 */
static unsigned int publish(int ret)
```

Python uses the same tags in `##` comment blocks, and CMake functions in
`#` comment blocks.

Where each comment goes:

- **Public functions** (declared in a header): the header holds the
  interface documentation above, at the declaration. The definition in the
  `.c` file has a plain `/*` comment (not a second doc block) saying it is
  part of the public interface and where that is documented, followed by
  any private implementation details a reader needs:

  ```c
  /*
   * Public interface: documented in chan_stats.h.
   *
   * Adds up every access point's power in mW (not dBm), each weighted by its
   * overlap with the channel (overlap_mhz()) over CHAN_WIDTH_MHZ.
   */
  void chan_stats_channel(const struct chan_stats *s, uint8_t channel, struct chan_info *out)
  ```

- **`static` functions and test cases** (`ZTEST`): the full doc block, at
  the definition. A test case's `@brief` says what it checks.

`scripts/check-docs.py` checks both: Doxygen's output for the doc blocks,
and the C sources for the comment above every definition.

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
