#include <cstdint>
#include <cstring>
#include <vector>

#include "doctest.h"
#include "krc/display/display_service.h"

using krc::display::Command;
using krc::display::DisplayMemory;
using krc::display::DisplayService;
using krc::display::kButtonLeft;
using krc::display::kStatusBadArgument;
using krc::display::kStatusEndOfInput;
using krc::display::kStatusNotReady;
using krc::display::kStatusOk;
using krc::display::kStatusUnknownCommand;
using krc::display::TextDisplay;

namespace {

// A flat guest memory: the service's canonical-block tests do not need a VM's address map.
class FlatMemory final : public DisplayMemory {
public:
    explicit FlatMemory(std::uint32_t size) : bytes(size, 0) {}
    [[nodiscard]] std::uint32_t size() const override {
        return static_cast<std::uint32_t>(bytes.size());
    }
    [[nodiscard]] bool valid_range(std::uint32_t address,
                                   std::uint32_t length) const override {
        return address <= size() && length <= size() - address;
    }
    bool read(std::uint32_t address, std::uint8_t* destination,
              std::uint32_t length) override {
        if (!valid_range(address, length)) return false;
        std::memcpy(destination, bytes.data() + address, length);
        return true;
    }
    bool write(std::uint32_t address, const std::uint8_t* source,
               std::uint32_t length) override {
        if (!valid_range(address, length)) return false;
        std::memcpy(bytes.data() + address, source, length);
        return true;
    }
    std::vector<std::uint8_t> bytes;
};

struct Fixture {
    TextDisplay display;
    FlatMemory memory{0x1000};
    std::uint32_t block[krc::display::kCanonicalWords] = {};

    std::int32_t call(Command command, std::uint32_t words) {
        return DisplayService::execute(display, memory, command, block, words);
    }
};

}  // namespace

TEST_CASE("the canonical word counts are fixed") {
    CHECK(DisplayService::required_words(Command::Info) == 4);
    CHECK(DisplayService::required_words(Command::Clear) == 0);
    CHECK(DisplayService::required_words(Command::SetColor) == 2);
    CHECK(DisplayService::required_words(Command::GetColor) == 2);
    CHECK(DisplayService::required_words(Command::PutChar) == 1);
    CHECK(DisplayService::required_words(Command::PutString) == 2);
    CHECK(DisplayService::required_words(Command::Cursor) == 3);
    CHECK(DisplayService::required_words(Command::ReadLine) == 2);
    CHECK(DisplayService::required_words(Command::GetKey) == 0);
    CHECK(DisplayService::required_words(Command::GetClick) == 3);
}

TEST_CASE("an unknown command or a wrong block size is a status") {
    Fixture fixture;
    CHECK(fixture.call(static_cast<Command>(10), 0) == kStatusUnknownCommand);
    CHECK(fixture.call(Command::Info, 3) == kStatusBadArgument);
    CHECK(fixture.call(Command::Clear, 1) == kStatusBadArgument);
    CHECK(fixture.call(Command::GetKey, 1) == kStatusBadArgument);
}

TEST_CASE("INFO answers the abi version and geometry") {
    Fixture fixture;
    CHECK(fixture.call(Command::Info, 4) == kStatusOk);
    CHECK(krc::display::kAbiVersion == 2);
    CHECK(fixture.block[0] == krc::display::kAbiVersion);
    CHECK(fixture.block[1] == 30);
    CHECK(fixture.block[2] == 80);
    CHECK(fixture.block[3] == 0);
}

TEST_CASE("colours round-trip through the canonical block") {
    Fixture fixture;
    fixture.block[0] = 0x11223344u;
    fixture.block[1] = 0x55667788u;
    CHECK(fixture.call(Command::SetColor, 2) == kStatusOk);
    CHECK(fixture.display.foreground() == 0x11223344u);
    CHECK(fixture.display.background() == 0x55667788u);
    fixture.block[0] = 0;
    fixture.block[1] = 0;
    CHECK(fixture.call(Command::GetColor, 2) == kStatusOk);
    CHECK(fixture.block[0] == 0x11223344u);
    CHECK(fixture.block[1] == 0x55667788u);
}

TEST_CASE("PUT_CHAR writes a cell and rejects a code wider than 16 bits") {
    Fixture fixture;
    fixture.block[0] = 0x41;
    CHECK(fixture.call(Command::PutChar, 1) == kStatusOk);
    CHECK(fixture.display.cell(0, 0).code == 0x41);
    CHECK(fixture.display.cursor_col() == 1);
    fixture.block[0] = 0x10041;
    CHECK(fixture.call(Command::PutChar, 1) == kStatusBadArgument);
}

TEST_CASE("PUT_STRING reads the guest buffer and clips the range") {
    Fixture fixture;
    fixture.memory.bytes[0x200] = 'h';
    fixture.memory.bytes[0x201] = 'i';
    fixture.block[0] = 0x200;
    fixture.block[1] = 2;
    CHECK(fixture.call(Command::PutString, 2) == kStatusOk);
    CHECK(fixture.display.cell(0, 0).code == 'h');
    CHECK(fixture.display.cell(0, 1).code == 'i');

    fixture.block[0] = 0x1000;
    fixture.block[1] = 1;
    CHECK(fixture.call(Command::PutString, 2) == kStatusBadArgument);
    fixture.block[0] = 0x0FFE;
    fixture.block[1] = 4;
    CHECK(fixture.call(Command::PutString, 2) == kStatusBadArgument);
}

TEST_CASE("CURSOR validates its arguments") {
    Fixture fixture;
    fixture.block[0] = 5;
    fixture.block[1] = 7;
    fixture.block[2] = 1;
    CHECK(fixture.call(Command::Cursor, 3) == kStatusOk);
    CHECK(fixture.display.cursor_row() == 5);
    CHECK(fixture.display.cursor_col() == 7);
    CHECK(fixture.display.cursor_visible());

    fixture.block[0] = 30;
    CHECK(fixture.call(Command::Cursor, 3) == kStatusBadArgument);
    fixture.block[0] = 0;
    fixture.block[1] = 80;
    CHECK(fixture.call(Command::Cursor, 3) == kStatusBadArgument);
    fixture.block[1] = 0;
    fixture.block[2] = 2;
    CHECK(fixture.call(Command::Cursor, 3) == kStatusBadArgument);
}

TEST_CASE("READ_LINE arms, polls and copies the completed line") {
    Fixture fixture;
    fixture.block[0] = 0x300;
    fixture.block[1] = 8;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusNotReady);
    CHECK(fixture.display.editor_active());

    fixture.display.push_text("ok\n");
    CHECK(fixture.call(Command::ReadLine, 2) == 2);
    CHECK(fixture.memory.bytes[0x300] == 'o');
    CHECK(fixture.memory.bytes[0x301] == 'k');
    CHECK(fixture.memory.bytes[0x302] == 0);  // the service writes only the line bytes
    CHECK_FALSE(fixture.display.line_ready());

    fixture.block[1] = 0;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusBadArgument);
    fixture.block[0] = 0x1000;
    fixture.block[1] = 4;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusBadArgument);
}

TEST_CASE("READ_LINE rejects a buffer shorter than a completed line") {
    Fixture fixture;
    fixture.block[0] = 0x300;
    fixture.block[1] = 16;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusNotReady);
    fixture.display.push_text("hello\n");
    fixture.block[1] = 3;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusBadArgument);
    CHECK(fixture.display.line_ready());
    fixture.block[1] = 16;
    CHECK(fixture.call(Command::ReadLine, 2) == 5);
}

TEST_CASE("GET_KEY and GET_CLICK report an empty queue as not ready") {
    Fixture fixture;
    CHECK(fixture.call(Command::GetKey, 0) == kStatusNotReady);
    CHECK(fixture.call(Command::GetClick, 3) == kStatusNotReady);

    fixture.display.push_key(0x41);
    CHECK(fixture.call(Command::GetKey, 0) == 0x41);
    CHECK(fixture.call(Command::GetKey, 0) == kStatusNotReady);

    fixture.display.push_click(3, 4, kButtonLeft);
    CHECK(fixture.call(Command::GetClick, 3) == kStatusOk);
    CHECK(fixture.block[0] == 3);
    CHECK(fixture.block[1] == 4);
    CHECK(fixture.block[2] == kButtonLeft);
    CHECK(fixture.call(Command::GetClick, 3) == kStatusNotReady);
}

TEST_CASE("CLEAR uses the current colours") {
    Fixture fixture;
    fixture.block[0] = 0x00FFFFFFu;
    fixture.block[1] = 0x000000FFu;
    CHECK(fixture.call(Command::SetColor, 2) == kStatusOk);
    CHECK(fixture.call(Command::Clear, 0) == kStatusOk);
    CHECK(fixture.display.cell(0, 0).foreground == 0x00FFFFFFu);
    CHECK(fixture.display.cell(0, 0).background == 0x000000FFu);
}

TEST_CASE("READ_LINE and GET_KEY report end of input after a close") {
    Fixture fixture;
    fixture.block[0] = 0x300;
    fixture.block[1] = 8;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusNotReady);
    fixture.display.push_text("ok\n");
    CHECK(fixture.call(Command::ReadLine, 2) == 2);
    fixture.display.close_input();
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusEndOfInput);
    CHECK(fixture.call(Command::GetKey, 0) == kStatusEndOfInput);

    // Argument validation is unchanged by the close: a bad buffer is still -1.
    fixture.block[1] = 0;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusBadArgument);
    fixture.block[1] = 8;
    fixture.block[0] = 0x1000;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusBadArgument);

    // Only input reports end of input; output still works.
    fixture.display.set_cursor(0, 0);
    fixture.block[0] = 0x41;
    CHECK(fixture.call(Command::PutChar, 1) == kStatusOk);
    CHECK(fixture.display.cell(0, 0).code == 0x41);
}

TEST_CASE("a line completed before a close is delivered before end of input") {
    Fixture fixture;
    fixture.block[0] = 0x300;
    fixture.block[1] = 8;
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusNotReady);
    fixture.display.push_text("hi\n");
    fixture.display.close_input();
    CHECK(fixture.call(Command::ReadLine, 2) == 2);
    CHECK(fixture.memory.bytes[0x300] == 'h');
    CHECK(fixture.memory.bytes[0x301] == 'i');
    CHECK(fixture.call(Command::ReadLine, 2) == kStatusEndOfInput);
}

TEST_CASE("queued keys drain before GET_KEY reports end of input") {
    Fixture fixture;
    fixture.display.push_key(0x41);
    fixture.display.push_key(0x42);
    fixture.display.close_input();
    CHECK(fixture.call(Command::GetKey, 0) == 0x41);
    CHECK(fixture.call(Command::GetKey, 0) == 0x42);
    CHECK(fixture.call(Command::GetKey, 0) == kStatusEndOfInput);
    // Clicks are a separate device and never report end of input.
    CHECK(fixture.call(Command::GetClick, 3) == kStatusNotReady);
}
