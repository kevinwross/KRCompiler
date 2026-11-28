#pragma once

#include <cstddef>
#include <cstdint>

// The common display ABI (docs/display-abi.md): command numbers, statuses, the canonical
// 32-bit argument block, key and button codes, and the cell/event types the shared device
// uses. A VM front end translates its own guest-visible block into this canonical form and
// calls DisplayService; nothing here names a VM or a hypercall number.

namespace krc::display {

// ABI version reported by INFO. A new command, a changed canonical block layout or a changed
// status value takes the next version; a guest must compare it before using a layout it does
// not know. Version 2 adds kStatusEndOfInput: READ_LINE and GET_KEY answer it when the host
// input source has been closed.
inline constexpr std::uint16_t kAbiVersion = 2;

// Reference geometry and colours. The surface is host state; these are the defaults a device
// starts with and the values the reference machines use.
inline constexpr std::uint16_t kDefaultRows = 30;
inline constexpr std::uint16_t kDefaultCols = 80;
inline constexpr std::uint32_t kDefaultForeground = 0xFFFFFFFFu;  // XRGB8888 white
inline constexpr std::uint32_t kDefaultBackground = 0xFF000000u;  // XRGB8888 black

// Commands of the common display family, in the order the ABI numbers them.
enum class Command : std::uint16_t {
    Info = 0,       // out: abi, rows, cols, reserved
    Clear = 1,      // no block
    SetColor = 2,   // in: foreground, background
    GetColor = 3,   // out: foreground, background
    PutChar = 4,    // in: code
    PutString = 5,  // in: address, byte count
    Cursor = 6,     // in: row, column, visible
    ReadLine = 7,   // in/out: buffer address, capacity; result length, -2 or -4
    GetKey = 8,     // no block; result key code, -2 or -4
    GetClick = 9,   // out: row, column, buttons; result 0 or -2
};

// Status or result in R2/the VM's result register, sign-extended. A non-negative READ_LINE or
// GET_KEY answer is not a status.
inline constexpr std::int32_t kStatusOk = 0;
inline constexpr std::int32_t kStatusBadArgument = -1;
inline constexpr std::int32_t kStatusNotReady = -2;
inline constexpr std::int32_t kStatusUnknownCommand = -3;
// The host input source was closed and nothing more will ever be queued. READ_LINE and
// GET_KEY answer it in place of kStatusNotReady once their queued input is drained; every
// other command is unaffected.
inline constexpr std::int32_t kStatusEndOfInput = -4;

// Key codes. Printable keys carry their ASCII code; the extended keys sit above it.
inline constexpr std::uint16_t kKeyBackspace = 8;
inline constexpr std::uint16_t kKeyTab = 9;
inline constexpr std::uint16_t kKeyEnter = 13;
inline constexpr std::uint16_t kKeyEscape = 27;
inline constexpr std::uint16_t kKeyLeft = 0x0102;
inline constexpr std::uint16_t kKeyRight = 0x0103;

// A guest-visible key code is 0x0000-0x7FFE: the negative range of a 16-bit result is how "no
// key" (-2) is reported, so a code that would read as negative is not representable.
inline constexpr std::uint16_t kMaximumKeyCode = 0x7FFEu;

// Click button mask. The wheel is not part of the ABI.
inline constexpr std::uint16_t kButtonLeft = 1;
inline constexpr std::uint16_t kButtonRight = 2;
inline constexpr std::uint16_t kButtonMiddle = 4;

// The canonical argument block has at most this many 32-bit words; a command names the exact
// word count it carries (CommandTable in display_service.h).
inline constexpr std::size_t kCanonicalWords = 4;

struct Cell {
    std::uint16_t code = 0;
    std::uint32_t foreground = kDefaultForeground;
    std::uint32_t background = kDefaultBackground;
};

struct Click {
    std::uint16_t row = 0;
    std::uint16_t col = 0;
    std::uint16_t buttons = 0;
};

struct Info {
    std::uint16_t abi_version = kAbiVersion;
    std::uint16_t rows = kDefaultRows;
    std::uint16_t cols = kDefaultCols;
};

}  // namespace krc::display
