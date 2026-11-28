# Display ABI

| | |
|---|---|
| Status | **Proposed, version 1** — implemented in KRCompiler's `krc::display` package and bound to a hypercall by `kr216` |
| Date | 2026-11-27T21:05:00-08:00 |
| Authority | The six user decisions of 2026-11-27 fix the device shape: 80x30 default, 16-bit cell codes with 8-bit guest strings, polled input, XRGB8888 with no alpha, left/right/middle mouse buttons, and the command set below |
| Applies to | KRCompiler's `krc::display` host library and `<krc/display.h>` guest header, each VM that binds the display family to a hypercall, and the debugger surfaces that render it |
| Related | `../kr216/kr216-display.md` records the kr216 binding and its ISA 1.x extension proposal |

The key words MUST, MUST NOT, SHOULD and MAY are used as in RFC 2119.

The display is a host-owned character device. A guest has no screen memory and no command
returns a cell: it writes through the command set below and reads input from polled queues.
The host owns the grid, the cursor, the line editor and the input events.

## 1. What is common and what is per-VM

| Layer | Owner |
|---|---|
| Command numbers, statuses, block field layouts, key and button codes, device semantics | This document |
| The hypercall number, the registers that carry command/block/length/result, and the mechanics of entering the host | Each VM (see §2) |
| The guest C API `<krc/display.h>` and the `display_*` wrappers | KRCompiler, implemented per VM under `targets/<vm>/runtime/` |
| The host device, dispatcher and framebuffer core | KRCompiler's `krc::display` package (§7) |
| Rendering the cells and forwarding input | A debugger front end (later task), not this ABI |

The display family is one reserved hypercall number per VM. The command number inside it is the
common axis; a VM MAY put the same family on a different hypercall and does not change the
command numbers.

## 2. VM binding

1. A VM that provides the display MUST reserve one hypercall number or trap/vector for the
   whole family. It MUST NOT allocate a new number per command.
2. On entry the VM carries the command number, the guest argument-block address and the
   block length in its own way; on return its result register carries the status or result
   below, as a signed value.
3. A VM without the display leaves the hypercall reserved, so a guest's capability probe traps
   or answers with a host-defined failure. There is no command outside the family that
   reveals a display.
4. The guest block belongs to the VM. Its length and layout are the VM's marshalling of the
   canonical block (§3); the command semantics do not change with the word size.
5. Statuses are `0`, `-1`, `-2`, `-3` (§4). A VM whose registers are 16 bits wide sign-extends
   the status into its result register.

`kr216` is the reference binding: SYS 5, R2 = command then result, R3 = block address,
R4 = block length, 16-bit little-endian words (`../kr216/kr216-display.md`).

## 3. Canonical argument block

The canonical block is an array of little-endian 32-bit words. A command names the exact
number of words it carries; a block of any other length is `-1`. All values are 32-bit; a
16-bit VM zero-extends the fields it marshals (§5.2).

| Command | # | Canonical words | In | Out |
|---|---:|---:|---|---|
| `INFO` | 0 | 4 | — | abi version, rows, cols, reserved (0) |
| `CLEAR` | 1 | 0 | — | — |
| `SET_COLOR` | 2 | 2 | foreground, background | — |
| `GET_COLOR` | 3 | 2 | — | foreground, background |
| `PUT_CHAR` | 4 | 1 | cell code | — |
| `PUT_STRING` | 5 | 2 | guest address, byte count | — |
| `CURSOR` | 6 | 3 | row, column, visible (0/1) | — |
| `READ_LINE` | 7 | 2 | guest buffer address, capacity | result length or `-2` |
| `GET_KEY` | 8 | 0 | — | result key code or `-2` |
| `GET_CLICK` | 9 | 3 | — | row, column, button mask |

Canonical block, word by word:

| Word | `INFO` | `SET_COLOR` / `GET_COLOR` | `PUT_STRING` | `CURSOR` | `READ_LINE` | `GET_CLICK` |
|---:|---|---|---|---|---|---|
| 0 | abi version | foreground | address | row | buffer | row |
| 1 | rows | background | byte count | column | capacity | column |
| 2 | cols | | | visible | | buttons |
| 3 | reserved (0) | | | | | |

`PUT_CHAR` word 0 is the cell code. `CLEAR` and `GET_KEY` carry no block; the VM ignores its
block registers for them.

## 4. Statuses

| Status | Value | Meaning |
|---|---:|---|
| `DISPLAY_OK` | 0 | the command succeeded |
| `DISPLAY_BAD_ARGUMENT` | -1 | bad block, bad length, bad pointer or bad argument value |
| `DISPLAY_NOT_READY` | -2 | no key/click is queued, or a line is still being edited |
| `DISPLAY_UNKNOWN_COMMAND` | -3 | the binding does not know the command number |

`READ_LINE` and `GET_KEY` return a non-negative result instead of `0`; only `-2` means
"nothing yet". An unknown command MUST answer `-3` rather than trap.

## 5. Field and buffer rules

### 5.1 Guest buffers

`PUT_STRING` reads the byte range `address .. address + byte count - 1`. `READ_LINE` writes
`capacity` bytes at its buffer address, and only the returned line bytes belong to the result.
A byte range that the VM cannot address in guest data memory is `-1`, never a fault; a VM
MUST validate the range before touching memory and MUST NOT route the copy through a device
or MMIO accessor. An empty `PUT_STRING` (count 0) succeeds and writes nothing.

The canonical service takes a `DisplayMemory` view from the VM: `valid_range`, `read` and
`write`. A VM implements it over the memory its guest can address; the address map's reserved
windows and null page stay outside it.

### 5.2 16-bit marshalling

A 16-bit target marshals the canonical fields as follows:

- A 32-bit field that fits 16 bits (`rows`, `cols`, `code`, `row`, `column`, `visible`,
  addresses, lengths, capacities) becomes one 16-bit word: the low 16 bits, zero-extended.
- A 32-bit colour becomes two words, low word first, high word second.
- `INFO` output is four 16-bit words: abi version, rows, cols, reserved.
- `SET_COLOR` input is four 16-bit words: foreground low/high, background low/high.
- `GET_COLOR` output is the same eight bytes. `GET_CLICK` output is three 16-bit words.
- The guest block length is a byte count of that 16-bit layout and is validated exactly, like
  the canonical word count.

## 6. Command semantics

### 6.1 `INFO`

Answers the ABI version and geometry. Version 1 is this document. A guest MUST read the
version and MUST NOT assume a layout it does not know. A new command or a changed block layout
takes the next version; new key or cell codes do not.

### 6.2 `CLEAR`

Fills every cell with the current colours and code 0. The cursor, the line editor and the
input queues are unchanged.

### 6.3 `SET_COLOR` / `GET_COLOR`

Colours are XRGB8888: the high byte is stored and returned verbatim and is not interpreted, so
there is no alpha blending. The default foreground is `0xFFFFFFFF` (white), the default
background `0xFF000000` (black). The colours apply to text written afterwards, to `CLEAR`, to
scrolling and to the line editor's echo.

### 6.4 `PUT_CHAR`

Stores one cell at the cursor and advances it. `0x08` Backspace moves left one column (never
across a row), `0x09` Tab advances to the next multiple of 8 columns (wrapping past the last
column), `0x0A` Line feed moves to column 0 of the next row (scrolling at the bottom), `0x0D`
Carriage return moves to column 0 of the current row. Other codes are stored as cells.

### 6.5 `PUT_STRING`

Each byte of the range is one 16-bit cell code, written through `PUT_CHAR`'s rules. The bytes
are 8-bit ASCII today; a future version may define UTF-8 without changing the count field.

### 6.6 `CURSOR`

Sets the cursor position and visibility. A position outside the grid, or a visibility other
than 0/1, is `-1` and leaves the cursor unchanged.

### 6.7 `READ_LINE`

Polled:

1. No line editing and no completed line: the host arms the line editor at the cursor and
   answers `-2`.
2. A line is being edited: every call answers `-2` and the editor state is unchanged.
3. A line is complete: the next call copies its bytes to the buffer (no terminator), answers
   the length and forgets the line.
4. A completed line longer than the caller's capacity is `-1` and the line stays pending.
5. A capacity of 0 or an invalid buffer range is `-1` without arming or consuming anything.

The editor caps a line at `capacity - 1` bytes and at the columns left on its row. The guest
only ever receives a completed line. The C wrapper NUL-terminates the buffer when the returned
length leaves room.

### 6.8 `GET_KEY`

Returns the oldest queued key, or `-2`. Key codes are `0x0000-0x7FFE`; the negative range of
a 16-bit result is how "no key" is reported.

| Key | Code |
|---|---:|
| Backspace | 0x08 |
| Tab | 0x09 |
| Enter | 0x0D |
| Escape | 0x1B |
| Printable ASCII | the ASCII code |
| Left arrow | 0x0102 |
| Right arrow | 0x0103 |

The host front end maps physical keys to these codes; codes in the range may grow without an
ABI version change.

### 6.9 `GET_CLICK`

Returns `0` with row, column and button mask when a click is queued, or `-2` with the block
untouched. Row and column are cell coordinates. Buttons: left 1, right 2, middle 4. There is
no wheel event in version 1.

### 6.10 Line editor

The host editor is armed by the first `READ_LINE` that finds no completed line and draws at
the arming cursor. Printable ASCII inserts at the caret; Backspace erases before it; Left and
Right move it; Enter completes and moves the cursor to the next row, column 0, leaving the echo
visible; Escape cancels, erases the echo and returns the cursor to the arming position. While
the editor is active it consumes every key, so a key it ignores is not queued for `GET_KEY`.

## 7. Host library: `krc::display`

KRCompiler exports the host side as `krc-display` (`krc::display`), a standard-library-only
static library installed like `krc::debug`:

```cmake
find_package(krc-display CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE krc::display)
```

```cpp
#include <krc/display/display_service.h>
#include <krc/display/surface.h>
#include <krc/display/text_display.h>
```

| Header | Contents |
|---|---|
| `krc/display/display_abi.h` | command numbers, statuses, key/button codes, `Cell`, `Click`, `Info`, defaults |
| `krc/display/text_display.h` | `TextDisplay`: cells, colours, cursor, line editor, key/click queues, polled results |
| `krc/display/display_service.h` | `DisplayMemory` (the VM's memory view) and `DisplayService::execute`, which implements the whole command set |
| `krc/display/surface.h` | `Surface`: XRGB8888 back/front buffers, mode validation, clipping, flip, frame hash, text overlay composition through a consumer-supplied `GlyphSource` |

A VM front end translates its guest hypercall into a canonical block, calls
`DisplayService::execute` with a `DisplayMemory` over its guest address space, and marshals the
result and output words back. That keeps every command's semantics in one place; the VM owns
only the block layout and the memory-map validation.

### 7.1 `Surface`

The framebuffer core is API and tests only in version 1: no VM adopts it yet and the 16-bit
machines stay text-only. It owns two XRGB8888 buffers, validates a mode before allocating,
clips every drawing call, commits with `flip()`, hashes the committed frame deterministically
(independently of host endianness), and composes a `TextDisplay` through a `GlyphSource`.
The package carries no font: a consumer supplies one.

## 8. Guest library

```c
#include <krc/display.h>

int display_clear(void);
int display_rows(void);
int display_cols(void);
int display_set_color(uint32_t foreground, uint32_t background);
int display_get_color(uint32_t *foreground, uint32_t *background);
int display_putc(int code);
int display_puts(const char *text);
int display_cursor(int row, int col, int visible);
int display_read_line(char *buffer, int capacity);
int display_get_key(void);
int display_get_click(int *row, int *col, int *buttons);
```

The header lives in the common sysroot (`sysroot/include/krc/display.h`) and is staged and
installed with the rest of the sysroot. The wrappers are per VM: `kr216`'s live in
`targets/kr216/runtime/display.c` and call the `__krc_display` trampoline in
`targets/kr216/runtime/syscalls.s`. A VM with a display adds its own wrapper file with the same
names and statuses.

## 9. Capability and compatibility

Version 1 is additive for every VM that adopts it:

- **Old guest, new host.** A program that never calls the display is unaffected.
- **New guest, old host.** The VM's display hypercall is reserved there and traps; a guest
  probes with `INFO`/`display_rows()` and treats a failure as "no display".
- **Version field.** `INFO` word 0 is the ABI version. A guest compares it before using a
  layout it does not know.

## 10. Version history

| Version | Date | Change |
|---|---|---|
| 0.1 | 2026-11-27 | First draft: common command set, canonical block, 16-bit marshalling, host and guest library, capability rule. |
