# 0064 — Display ABI version 2: end of input

- Status: accepted
- Date: 2026-11-27
- Decision: how a guest observes that the host display's input source has closed
- Scope: `docs/display-abi.md`, the `krc::display` package (`display_abi.h`, `TextDisplay`, `DisplayService`) and the guest header `<krc/display.h>`. No VM hypercall, ISA encoding, object format, symbol format, target descriptor or IR change; the kr216 binding stays SYS 5.

## Context

Display ABI version 1 has no end-of-input: `READ_LINE` answers a completed line length or
`-2` while the line is still being edited, and `GET_KEY` answers a key code or `-2` when its
queue is empty. There is no value for "nothing will ever arrive again". A VM host that owns
the display can still reach end of file — the kr216 emulator exhausts a scripted
`--display-input`, the debugger's DISPLAY view has a close gesture — but the guest cannot see
it. A `getchar` loop polls a line editor that will never complete a line, and a program whose
input model is "read until EOF" (a game loop, `tictactoe`, a filter) cannot terminate. A
terminal without a close is not a terminal.

The alternatives were considered and rejected:

- **Host convention: answer `-1`.** `-1` is `DISPLAY_BAD_ARGUMENT`; reusing it for end of
  input hides a real argument error and makes the version 1 status table wrong.
- **Copy the console's model.** The console has no status channel either (an empty read is
  zero bytes), and the whole point of the split is that the display, not the console, is
  stdin.
- **No EOF, document the limitation.** Correct for a purely interactive front end, but it
  leaves scripted runs and the C library unable to terminate, which is the case this change
  exists for.

## Decision

1. **Version 2 adds `DISPLAY_END_OF_INPUT = -4`.** `INFO` reports ABI version 2. Only
   `READ_LINE` and `GET_KEY` answer `-4`; `-1` stays bad block/buffer/argument/line-too-long
   and `-2` stays "nothing yet" while the source is open.
2. **The host closes the source explicitly** with `krc::display::TextDisplay::close_input()`.
   A closed source ignores further keys and text, completes a non-empty editor draft as its
   final line (as a terminal does with an unterminated last line at end of file) and cancels an
   empty draft. `reset()` reopens it. Closing is host state, not a guest command.
3. **`READ_LINE` and `GET_KEY` answer `-4` only once their queued input is drained.** A line
   completed before the close is delivered first; a key queued before the close is returned
   first. The click queue is a separate device and is unaffected.
4. **The C runtime maps `-4` to end of file.** kr216's `getchar`/`getc` return `-1` for it;
   kr232 does the same when it routes stdin to the display.
5. **`-4` is additive for version 1 guests that do not read input.** A version 1 guest that
   does read input and checks only for `-2` must treat an unknown negative answer as "no
   input"; that caveat is recorded in `docs/display-abi.md` §9.

## Consequences

- The display ABI, guest header, host library and docs carry version 2; consumers built against
  a version 1 prefix must be reconfigured against a prefix that has version 2 before they use
  `close_input()`, `kStatusEndOfInput` or `DISPLAY_END_OF_INPUT`.
- The kr216 emulator's `--display-input` closes the source when its script is exhausted, which
  is how a headless guest test reaches EOF; the debugger front end keeps the source open and
  closes it on its own close/EOF gesture.
- Host unit tests cover `close_input()` and the `-4` statuses for both commands, including the
  drain order, the completed-line-first rule and `reset()` reopening.
- A future version can add commands or statuses; `-4` does not take another version because it
  is part of version 2.
