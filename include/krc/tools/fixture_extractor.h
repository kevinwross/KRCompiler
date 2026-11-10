#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace krc::tools {

struct ExtractedFixture {
    std::string source_name;
    std::uint32_t line = 1;
    std::string text;
};

// Extracts C/C++ source passed to compile/compile_and_run-style calls in a
// doctest translation unit. Adjacent string literals are concatenated.
[[nodiscard]] std::vector<ExtractedFixture> extract_fixtures(std::string_view source,
                                                             std::string_view source_name);

}  // namespace krc::tools
