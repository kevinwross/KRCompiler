#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "doctest.h"
#include "krc/target/sysroot_bits.h"
#include "krc/target/target.h"

#ifndef KRC_SOURCE_DIR
#define KRC_SOURCE_DIR "."
#endif

namespace {

// Git may check a text file out with CRLF on a machine whose core.autocrlf is
// true, so a byte comparison against generated text depends on the checkout's
// end-of-line translation rather than on the content under test. Strip the
// CR of CRLF pairs; the generator always emits LF.
std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    const std::istreambuf_iterator<char> begin(file);
    const std::istreambuf_iterator<char> end;
    const std::string text(begin, end);
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            continue;
        }
        normalized.push_back(text[index]);
    }
    return normalized;
}

}  // namespace

TEST_CASE("generated target bits match the checked-in sysroot headers") {
    std::size_t checked = 0;
    for (const auto& profile : krc::target::target_profiles()) {
        const std::filesystem::path directory =
            std::filesystem::path(KRC_SOURCE_DIR) / "sysroot" / std::string(profile.name) /
            "include" / "bits";
        if (!std::filesystem::exists(directory)) {
            // kr232's sysroot is M3's deliverable. A target without headers has
            // nothing to compare, so the absence is allowed only for kr232 and
            // the count below keeps it from becoming a silent skip.
            CHECK(profile.id == krc::target::TargetId::Kr232);
            continue;
        }
        INFO("target: " << profile.name);
        ++checked;
        CHECK(krc::target::generate_bits_types(profile) ==
              read_file(directory / "types.h"));
        CHECK(krc::target::generate_bits_stdint(profile) ==
              read_file(directory / "stdint.h"));
    }
    CHECK(checked == 4);
}
