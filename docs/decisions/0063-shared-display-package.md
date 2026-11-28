# 0063 — The display device is a shared krc-display package

- Status: accepted
- Date: 2026-11-27
- Decision: where the host display device and the display ABI live, and how a VM binds to them
- Scope: KRCompiler adds the `krc::display` package, `<krc/display.h>`, `docs/display-abi.md` and the kr216 runtime wrappers; kr216 consumes the installed package and keeps only the SYS 5 front end. No ISA encoding, object format, symbol format, target descriptor or IR change.

## Context

The kr216 text display was first implemented inside kr216: a `TextDisplay` class in its
`include/kr216`, the SYS 5 dispatch in `Machine`, and the guest wrappers in the kr216 runtime.
That makes the device a kr216-private concern even though the debugger surface, and the
32-bit VMs that will later want a framebuffer, need the same cells, colours, cursor, line
editor and input queues. Two copies of a device whose semantics must agree is a correctness
risk, and the owner approved moving it to one package that the VMs consume.

## Decision

1. **The host device is `krc-display` in KRCompiler**, exported as `krc::display` and installed
   like `krc::debug`: headers under `include/krc/display`, the `krc-display` static library,
   and a `krc-display-config.cmake` package config in the shared `krc-targets` export set.
   It depends only on the C++ standard library; a VM or a debugger links it without the
   compiler.
2. **The package owns three layers.** `TextDisplay` is the character device. `Surface` is the
   XRGB8888 framebuffer core (back/front buffers, mode validation, clipping, flip, frame
   hash, text overlay composition through a consumer-supplied `GlyphSource`); no VM adopts it
   in version 1 and the 16-bit machines stay text-only. `DisplayService` dispatches the
   command set through a `DisplayMemory` view, so every command's semantics and every
   validation rule live in one place.
3. **The command set and canonical block are specified in `docs/display-abi.md`.** Each VM
   reserves one hypercall number for the whole family and marshals the canonical 32-bit
   argument block in its own word size; kr216's binding is SYS 5 with a 16-bit block and is
   recorded in `../kr216/kr216-display.md`.
4. **The guest API is common and target-neutral**: `<krc/display.h>` in the common sysroot
   declares `display_clear/rows/cols/set_color/get_color/putc/puts/cursor/read_line/get_key/
   get_click`; each VM supplies those wrappers in its runtime. The common header and the
   staged sysroot install them with the rest of the sysroot.
5. **kr216 becomes a consumer.** Its `Machine` owns a `krc::display::TextDisplay`, exposes
   `display()`, and its SYS 5 handler is a thin translator between the 16-bit guest block and
   the canonical block plus a `DisplayMemory` over the 64 KiB. The kr216 host build now
   requires `find_package(krc-display CONFIG REQUIRED)` from the installed prefix.

## Consequences

- The family build must install KRCompiler before a consumer configures; kr216's guest tests
  and host tests need the matching configuration prefix, as every other consumer does
  (Debug prefix for Debug, Release prefix for Release).
- Adding a display to a VM is now a wrapper file and a block translation, not a device
  implementation.
- The device's unit tests live in KRCompiler (`tests/unit/test_display_*.cpp`), the ABI
  marshalling evidence stays with the VM front end, and a package-consumer test installs the
  build tree into a private prefix and builds an external CMake project against
  `find_package(krc-display)`.
- A future krdbg DISPLAY view consumes `krc::display::TextDisplay`; it is a later task.
- The kr216 ISA extension is unchanged and still proposed: SYS 5 claims a reserved number in
  the 1.x line, and the frozen `kr216-isa.md` is not edited here.
