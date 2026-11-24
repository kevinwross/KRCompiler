#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "krc/debug/kdbg.h"
#include "krc/toolchain.h"
#include "linker/kro_object.h"

namespace {

krc::toolchain::Source source(std::string name, std::string_view text) {
    return krc::toolchain::Source{std::move(name), text};
}

// A host's own `.lst` format. The facade returns rows; this function is what a
// VM front end would write, and it exists here to show the row model is enough
// to render one.
std::string format_listing(const std::vector<krc::toolchain::ListingRow>& rows) {
    std::string output;
    for (const auto& row : rows) {
        if (row.bytes.empty()) {
            continue;
        }
        char address[9] = {};
        std::snprintf(address, sizeof(address), "%08llX",
                      static_cast<unsigned long long>(row.address));
        output += address;
        output += "  ";
        for (const auto byte : row.bytes) {
            static constexpr char digits[] = "0123456789ABCDEF";
            output.push_back(digits[byte >> 4]);
            output.push_back(digits[byte & 0x0F]);
            output.push_back(' ');
        }
        output += " ";
        output += row.text;
        output.push_back('\n');
    }
    return output;
}

class EmptyWorkingDirectory {
public:
    EmptyWorkingDirectory() {
        std::error_code error;
        path_ = std::filesystem::current_path(error) / "krc-toolchain-cwd";
        if (error) {
            message_ = error.message();
            return;
        }
        std::filesystem::remove_all(path_, error);
        error.clear();
        std::filesystem::create_directories(path_, error);
        if (error) {
            message_ = error.message();
            return;
        }
        previous_ = std::filesystem::current_path(error);
        if (error) {
            message_ = error.message();
            return;
        }
        std::filesystem::current_path(path_, error);
        if (error) {
            message_ = error.message();
            return;
        }
        ok_ = true;
    }

    ~EmptyWorkingDirectory() {
        std::error_code error;
        if (ok_) {
            std::filesystem::current_path(previous_, error);
        }
        std::filesystem::remove_all(path_, error);
    }

    EmptyWorkingDirectory(const EmptyWorkingDirectory&) = delete;
    EmptyWorkingDirectory& operator=(const EmptyWorkingDirectory&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& message() const { return message_; }

    [[nodiscard]] std::size_t entry_count() const {
        std::error_code error;
        std::size_t count = 0;
        for (std::filesystem::directory_iterator it(path_, error);
             !error && it != std::filesystem::directory_iterator(); ++it) {
            ++count;
        }
        return count;
    }

private:
    std::filesystem::path path_;
    std::filesystem::path previous_;
    std::string message_;
    bool ok_ = false;
};

}  // namespace

TEST_CASE("The facade builds an image from text with no files") {
    krc::toolchain::Options options;
    const auto result = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("answer.c", "int main() { return 42; }")},
        options);
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK(result.diagnostics.empty());
    REQUIRE(result.image.size() > 64);
    CHECK_EQ(result.image[0], 'K');
    CHECK_EQ(result.image[1], '3');
    CHECK_EQ(result.image[2], '2');
    CHECK(result.entry != 0);
    CHECK_FALSE(result.listing.empty());
}

TEST_CASE("Two sequential builds are byte-identical") {
    krc::toolchain::Options options;
    const std::vector<krc::toolchain::Source> sources{
        source("twice.c", "int add(int a, int b) { return a + b; }\nint main() { return add(1, 2); }")};
    const auto first = krc::toolchain::build(sources, options);
    const auto second = krc::toolchain::build(sources, options);
    REQUIRE_MESSAGE(first.ok, first.error);
    REQUIRE_MESSAGE(second.ok, second.error);
    CHECK(first.image == second.image);
    REQUIRE_EQ(first.listing.size(), second.listing.size());
    for (std::size_t index = 0; index < first.listing.size(); ++index) {
        CHECK_EQ(first.listing[index].address, second.listing[index].address);
        CHECK_EQ(first.listing[index].bytes, second.listing[index].bytes);
        CHECK_EQ(first.listing[index].text, second.listing[index].text);
    }
}

TEST_CASE("A build from strings leaves the working directory empty") {
    EmptyWorkingDirectory directory;
    REQUIRE_MESSAGE(directory.ok(), directory.message());
    krc::toolchain::Options options;
    const auto result = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("cwd.c", "int main() { return 0; }")},
        options);
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK_EQ(directory.entry_count(), 0);
}

TEST_CASE("The listing reproduces the assembly statements and addresses") {
    const std::string assembly =
        ".text\n"
        ".Ldbg.main.3.1:\n"
        "main:\n"
        "    MOV R0, #42\n"
        "    RET\n";
    krc::toolchain::Options options;
    const auto assembled = krc::toolchain::assemble(assembly, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);

    const auto find_row = [&](std::string_view text) -> const krc::toolchain::ListingRow* {
        for (const auto& row : assembled.listing) {
            if (row.text == text) {
                return &row;
            }
        }
        return nullptr;
    };
    const auto* move = find_row("MOV R0, #42");
    const auto* ret = find_row("RET");
    REQUIRE(move != nullptr);
    REQUIRE(ret != nullptr);
    CHECK_EQ(move->section, ".text");
    CHECK_EQ(move->source_line, 3);
    CHECK_EQ(move->bytes.size(), 4);
    CHECK_EQ(ret->address, move->address + 4);

    const auto listing = format_listing(assembled.listing);
    CHECK(listing.find("MOV R0, #42") != std::string::npos);
    CHECK(listing.find("00000000") != std::string::npos);
}

TEST_CASE("A linked build places its listing at final addresses") {
    krc::toolchain::Options options;
    const auto result = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("final.c", "int main() { return 7; }")},
        options);
    REQUIRE_MESSAGE(result.ok, result.error);

    std::uint64_t text_address = 0;
    std::uint64_t text_size = 0;
    for (const auto& section : result.sections) {
        if (section.name == ".text") {
            text_address = section.address;
            text_size = section.size;
        }
    }
    REQUIRE(text_size != 0);
    bool saw_instruction = false;
    for (const auto& row : result.listing) {
        if (row.section != ".text" || row.bytes.empty()) {
            continue;
        }
        saw_instruction = true;
        CHECK(row.address >= text_address);
        CHECK(row.address + row.bytes.size() <= text_address + text_size);
    }
    CHECK(saw_instruction);
}

TEST_CASE("Diagnostics are data a host can render after the call") {
    krc::toolchain::Options options;
    const auto result = krc::toolchain::compile(
        source("broken.c", "int main() { return missing; }"), options);
    CHECK_FALSE(result.ok);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().file == "broken.c");
    CHECK(result.diagnostics.front().line == 1);
    const auto text = krc::toolchain::format_diagnostics(result.diagnostics);
    CHECK(text.find("broken.c:1:") != std::string::npos);
    CHECK(text.find("error:") != std::string::npos);
}

TEST_CASE("The facade compiles, assembles and links a debuggable KR16 image") {
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr16;
    const std::string_view text =
        "int add(int a, int b) {\n"
        "  int sum = a + b;\n"
        "  return sum;\n"
        "}\n"
        "int main() { return add(20, 22); }\n";
    const auto compiled = krc::toolchain::compile(source("kr16_debug.c", text), options);
    REQUIRE_MESSAGE(compiled.ok, compiled.error);
    CHECK_FALSE(compiled.assembly.empty());
    CHECK_FALSE(compiled.debug_info.empty());

    const auto assembled =
        krc::toolchain::assemble(compiled.assembly, compiled.debug_info, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    CHECK_FALSE(assembled.object.empty());

    const auto built = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("kr16_debug.c", text)}, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    REQUIRE(built.image.size() > 64);
    CHECK_EQ(built.image[0], 'K');
    CHECK_EQ(built.image[1], 'R');
    CHECK_EQ(built.image[2], '1');
    CHECK_EQ(built.image[3], '6');
    CHECK_EQ(built.base, 0x100);
    // The entry is `main`'s address, not the load base: the fixture's first
    // function is `add`.
    CHECK(built.entry > built.base);

    // 4.10: a krc-built `.kr16` decodes with sources, lines, functions and a
    // variable, and the payload records the base it was built for.
    krc::debug::KdbgImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(built.image, image, error), error);
    CHECK_EQ(image.target_id, 2);
    CHECK_EQ(image.image_base, 0x100);
    REQUIRE_EQ(image.sources.size(), 1);
    CHECK_EQ(image.sources[0].path, "kr16_debug.c");
    CHECK_FALSE(image.lines.empty());
    CHECK_FALSE(image.functions.empty());

    bool saw_add = false;
    for (const auto& function : image.functions) {
        saw_add = saw_add || function.name == "add";
    }
    CHECK(saw_add);

    bool saw_variable = false;
    for (const auto& variable : image.variables) {
        saw_variable = saw_variable ||
                       (variable.function == "add" && variable.name == "sum");
    }
    CHECK(saw_variable);

    bool saw_line = false;
    for (const auto& line : image.lines) {
        saw_line = saw_line || line.line == 2;
    }
    CHECK(saw_line);

    // The bridge and its maps are per-emission state; a second build of the same
    // request must not observe the first one.
    const auto again = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("kr16_debug.c", text)}, options);
    REQUIRE_MESSAGE(again.ok, again.error);
    CHECK(again.image == built.image);
}

// 4.15: a declaration is not a statement. The alloca for `int local;` used to
// carry a line-2 label at the same address as the `local = 7;` store, so a
// debugger stopping there reported line 2 for line 3.
TEST_CASE("A KR16 declaration does not take the next statement's line") {
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr16;
    const std::string_view text =
        "int main(void) {\n"
        "    int local;\n"
        "    local = 7;\n"
        "    return local;\n"
        "}\n";
    const auto built = krc::toolchain::build(
        std::vector<krc::toolchain::Source>{source("line3.c", text)}, options);
    REQUIRE_MESSAGE(built.ok, built.error);

    krc::debug::KdbgImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg_image(built.image, image, error), error);
    bool saw_line_2 = false;
    bool saw_line_3 = false;
    for (const auto& line : image.lines) {
        saw_line_2 = saw_line_2 || line.line == 2;
        saw_line_3 = saw_line_3 || line.line == 3;
    }
    CHECK_FALSE(saw_line_2);
    CHECK(saw_line_3);
    // The first line-bearing instruction of `main` is the store, not the
    // declaration: the function's start is the prologue.
    std::uint64_t first_line_address = 0;
    std::uint32_t first_line = 0;
    for (const auto& function : image.functions) {
        if (function.name != "main") {
            continue;
        }
        for (const auto& line : image.lines) {
            if (line.address < function.start) {
                continue;
            }
            if (first_line_address == 0 || line.address < first_line_address) {
                first_line_address = line.address;
                first_line = line.line;
            }
        }
    }
    CHECK_EQ(first_line, 3);
}
TEST_CASE("flatten places allocated sections at their linked addresses") {
    // A1 of the build cleanup plan: the flat image is what a boot loader reads,
    // so the bytes must land at the addresses the linker chose and everything
    // between sections must be zero rather than uninitialised.
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr32;
    options.entry = "_start";
    options.base = 0x1000;

    const std::string text =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  MOV R2, #7\n"
        ".data\n"
        ".global value\n"
        "value:\n"
        "  .WORD 0x11223344\n";
    const auto assembled = krc::toolchain::assemble(text, options);
    REQUIRE(assembled.ok);

    const std::vector<krc::toolchain::Object> objects{{"flat_probe.s", assembled.object}};
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE(linked.ok);

    // Both sections occupy memory, so both carry bytes; a debug section would not.
    std::uint64_t end = linked.base;
    std::size_t allocated = 0;
    for (const auto& section : linked.sections) {
        if (section.bytes.empty()) {
            continue;
        }
        ++allocated;
        end = std::max(end, section.address + section.size);
    }
    REQUIRE(allocated >= 2);

    const auto flat = krc::toolchain::flatten(linked);
    REQUIRE(flat.size() == end - linked.base);

    // The data word is stored little-endian at its own address.
    const auto data = std::find_if(linked.sections.begin(), linked.sections.end(),
                                   [](const krc::toolchain::SectionInfo& section) {
                                       return section.name == ".data";
                                   });
    REQUIRE(data != linked.sections.end());
    REQUIRE(data->size >= 4);
    const auto offset = static_cast<std::size_t>(data->address - linked.base);
    CHECK(flat[offset + 0] == 0x44);
    CHECK(flat[offset + 1] == 0x33);
    CHECK(flat[offset + 2] == 0x22);
    CHECK(flat[offset + 3] == 0x11);

    // Every byte outside an allocated section is zero.
    std::vector<bool> covered(flat.size(), false);
    for (const auto& section : linked.sections) {
        if (section.bytes.empty()) {
            continue;
        }
        const auto start = static_cast<std::size_t>(section.address - linked.base);
        for (std::size_t index = 0; index < section.size; ++index) {
            covered[start + index] = true;
        }
    }
    for (std::size_t index = 0; index < flat.size(); ++index) {
        if (!covered[index]) {
            CHECK(flat[index] == 0);
        }
    }
}
TEST_CASE("the symbol sidecar carries the image symbols and a CRC of the flat form") {
    // A2 of the build cleanup plan. The kr32 OS reads this sidecar, and its
    // consumer (kr32/include/kr32/os/symbols.h) refuses one whose `__sym_crc`
    // does not equal the CRC32 of the flat image it placed in memory.
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr32;
    options.entry = "_start";
    options.base = 0x1000;
    options.symbol_sidecar = true;

    const std::string text =
        ".text\n"
        ".global _start\n"
        "_start:\n"
        "  MOV R2, #7\n"
        ".data\n"
        ".global value\n"
        "value:\n"
        "  .WORD 0x11223344\n";
    const auto assembled = krc::toolchain::assemble(text, options);
    REQUIRE(assembled.ok);
    const std::vector<krc::toolchain::Object> objects{{"sidecar_probe.s", assembled.object}};
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE(linked.ok);
    REQUIRE_FALSE(linked.sidecar.empty());

    // It is a kr32 container: magic "K32\0".
    REQUIRE(linked.sidecar.size() > 4);
    CHECK(linked.sidecar[0] == 'K');
    CHECK(linked.sidecar[1] == '3');
    CHECK(linked.sidecar[2] == '2');
    CHECK(linked.sidecar[3] == 0x00);

    // The checksum section is present, and its four bytes are the CRC32 of the
    // flat image, which is what the OS compares against.
    const std::string sidecar_text(linked.sidecar.begin(), linked.sidecar.end());
    CHECK(sidecar_text.find("__sym_crc") != std::string::npos);
    const auto flat = krc::toolchain::flatten(linked);
    const auto checksum = krc::linker::crc32(flat);
    bool found = false;
    for (std::size_t index = 0; index + 4 <= linked.sidecar.size(); ++index) {
        const auto value = static_cast<std::uint32_t>(linked.sidecar[index]) |
                           (static_cast<std::uint32_t>(linked.sidecar[index + 1]) << 8) |
                           (static_cast<std::uint32_t>(linked.sidecar[index + 2]) << 16) |
                           (static_cast<std::uint32_t>(linked.sidecar[index + 3]) << 24);
        if (value == checksum) {
            found = true;
            break;
        }
    }
    CHECK(found);
}
