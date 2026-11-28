#include "krc/display/display_service.h"

#include <algorithm>
#include <array>

namespace krc::display {
namespace {

// The largest line and the largest guest string a front end can ask for through the canonical
// block. A 16-bit VM cannot exceed this, and the service keeps the limit explicit so a 32-bit
// guest cannot make a host read an unbounded buffer.
constexpr std::uint32_t kMaximumTransfer = 0xFFFFu;

}  // namespace

std::uint32_t DisplayService::required_words(Command command) noexcept {
    switch (command) {
    case Command::Info:
        return 4;
    case Command::Clear:
        return 0;
    case Command::SetColor:
        return 2;
    case Command::GetColor:
        return 2;
    case Command::PutChar:
        return 1;
    case Command::PutString:
        return 2;
    case Command::Cursor:
        return 3;
    case Command::ReadLine:
        return 2;
    case Command::GetKey:
        return 0;
    case Command::GetClick:
        return 3;
    }
    return 0;
}

std::int32_t DisplayService::execute(TextDisplay& display, DisplayMemory& memory,
                                     Command command, std::uint32_t* block,
                                     std::uint32_t words) {
    if (words != required_words(command)) {
        return kStatusBadArgument;
    }

    switch (command) {
    case Command::Info: {
        const Info info = display.info();
        block[0] = info.abi_version;
        block[1] = info.rows;
        block[2] = info.cols;
        block[3] = 0;
        return kStatusOk;
    }
    case Command::Clear:
        display.clear();
        return kStatusOk;
    case Command::SetColor:
        display.set_color(block[0], block[1]);
        return kStatusOk;
    case Command::GetColor:
        block[0] = display.foreground();
        block[1] = display.background();
        return kStatusOk;
    case Command::PutChar:
        if (block[0] > 0xFFFFu) return kStatusBadArgument;
        display.put(static_cast<std::uint16_t>(block[0]));
        return kStatusOk;
    case Command::PutString: {
        const std::uint32_t address = block[0];
        const std::uint32_t count = block[1];
        if (count > kMaximumTransfer) return kStatusBadArgument;
        if (address > memory.size() || count > memory.size() - address) {
            return kStatusBadArgument;
        }
        std::array<std::uint8_t, 256> bytes{};
        for (std::uint32_t offset = 0; offset < count;) {
            const std::uint32_t chunk = std::min<std::uint32_t>(
                static_cast<std::uint32_t>(bytes.size()), count - offset);

            if (!memory.read(address + offset, bytes.data(), chunk)) {
                return kStatusBadArgument;
            }
            for (std::uint32_t index = 0; index < chunk; ++index) {
                display.put(static_cast<std::uint16_t>(bytes[index]));
            }
            offset += chunk;
        }
        return kStatusOk;
    }
    case Command::Cursor: {
        const std::uint32_t row = block[0];
        const std::uint32_t col = block[1];
        const std::uint32_t visible = block[2];
        if (row > 0xFFFFu || col > 0xFFFFu || row >= display.rows() ||
            col >= display.cols() || visible > 1) {
            return kStatusBadArgument;
        }
        display.set_cursor(static_cast<std::uint16_t>(row), static_cast<std::uint16_t>(col));
        display.set_cursor_visible(visible != 0);
        return kStatusOk;
    }
    case Command::ReadLine: {
        const std::uint32_t buffer = block[0];
        const std::uint32_t capacity = block[1];
        if (capacity == 0 || capacity > kMaximumTransfer) return kStatusBadArgument;
        if (!memory.valid_range(buffer, capacity)) return kStatusBadArgument;
        if (!display.line_ready()) {
            if (display.input_closed()) return kStatusEndOfInput;
            display.arm_line_editor(static_cast<std::uint16_t>(capacity));
            return kStatusNotReady;
        }
        if (display.pending_line_length() > capacity) return kStatusBadArgument;
        const std::optional<std::string> line = display.take_line();
        if (!memory.write(buffer, reinterpret_cast<const std::uint8_t*>(line->data()),
                          static_cast<std::uint32_t>(line->size()))) {
            return kStatusBadArgument;
        }
        return static_cast<std::int32_t>(line->size());
    }
    case Command::GetKey: {
        const int key = display.get_key();
        if (key == kStatusNotReady && display.input_closed()) return kStatusEndOfInput;
        if (key < 0 || key > kMaximumKeyCode) return kStatusNotReady;
        return key;
    }
    case Command::GetClick: {
        Click click;
        if (!display.take_click(click)) return kStatusNotReady;
        block[0] = click.row;
        block[1] = click.col;
        block[2] = click.buttons;
        return kStatusOk;
    }
    }
    return kStatusUnknownCommand;
}

}  // namespace krc::display
