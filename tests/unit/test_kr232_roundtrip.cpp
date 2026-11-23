#include "doctest.h"

#include "asm/kr232_parser.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef KRC_SOURCE_DIR
#define KRC_SOURCE_DIR "."
#endif

using krc::asm_kr232::parse_instruction;

// The fixture is generated from kr232-isa.md Appendix A, one canonical row per
// vector: `assembly<TAB>hex bytes`. The seven `(generic)` alternates are excluded
// because they encode the same text through `F0`, which the encoder's generic
// cases already cover; including them would give one text two encodings.
TEST_CASE("every Appendix A vector assembles to its documented bytes") {
    const std::filesystem::path path =
        std::filesystem::path(KRC_SOURCE_DIR) / "tests" / "fixtures" / "kr232-vectors.txt";
    std::ifstream input(path);
    REQUIRE_MESSAGE(input.good(), path.string());

    std::string line;
    std::size_t count = 0;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t tab = line.find('\t');
        REQUIRE_MESSAGE(tab != std::string::npos, line);
        const std::string text = line.substr(0, tab);
        const std::string hex = line.substr(tab + 1);
        std::vector<std::uint8_t> expected;
        for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
            expected.push_back(
                static_cast<std::uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
        }
        ++count;

        std::string error;
        const auto encoded = parse_instruction(text, error);
        INFO(text);
        REQUIRE_MESSAGE(encoded.has_value(), text << ": " << error);
        CHECK(encoded->length == expected.size());
        for (std::size_t i = 0; i < expected.size() && i < encoded->bytes.size(); ++i) {
            CHECK(encoded->bytes[i] == expected[i]);
        }
    }
    CHECK(count == 71);
}
