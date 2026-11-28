#pragma once

#include <cstdint>

#include "krc/display/display_abi.h"
#include "krc/display/text_display.h"

namespace krc::display {

// One VM's guest memory as the display service sees it. A front end implements this over the
// memory its guest can address; the service uses it for the two commands that carry guest
// buffers (PUT_STRING reads, READ_LINE writes) and never for the argument block itself, which
// the front end marshals into the canonical form.
//
// valid_range() and the accessors must agree: a range valid_range() rejects makes read() and
// write() return false. A bad address is a status (-1), never a fault.
class DisplayMemory {
public:
    virtual ~DisplayMemory() = default;
    [[nodiscard]] virtual std::uint32_t size() const = 0;
    [[nodiscard]] virtual bool valid_range(std::uint32_t address,
                                           std::uint32_t length) const = 0;
    virtual bool read(std::uint32_t address, std::uint8_t* destination,
                      std::uint32_t length) = 0;
    virtual bool write(std::uint32_t address, const std::uint8_t* source,
                       std::uint32_t length) = 0;
};

// The display command dispatcher: the one place the common command set is implemented, so a
// VM's front end only translates its guest-visible block into the canonical 32-bit block and
// its result register back.
//
// The canonical block word counts are fixed by docs/display-abi.md:
//
//   INFO 4, CLEAR 0, SET_COLOR 2, GET_COLOR 2, PUT_CHAR 1, PUT_STRING 2,
//   CURSOR 3, READ_LINE 2, GET_KEY 0, GET_CLICK 3
//
// The block is host memory, not guest memory: the front end fills it from its own guest block
// on entry and copies the out words back on return. All values are little-endian 32-bit
// words; a 16-bit VM zero-extends each marshalled field.
class DisplayService {
public:
    // The canonical word count a command carries; 0 for a command with no block and for an
    // unknown command number.
    [[nodiscard]] static std::uint32_t required_words(Command command) noexcept;

    // Executes one command. words must equal required_words(command) for a known command;
    // otherwise the answer is kStatusBadArgument (an unknown command is
    // kStatusUnknownCommand). The return value is the status or result the VM puts in its
    // result register.
    static std::int32_t execute(TextDisplay& display, DisplayMemory& memory, Command command,
                                std::uint32_t* block, std::uint32_t words);
};

}  // namespace krc::display
