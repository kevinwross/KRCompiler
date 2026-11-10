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

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("generated target bits match the checked-in sysroot headers") {
    for (const auto& profile : krc::target::target_profiles()) {
        const std::filesystem::path directory =
            std::filesystem::path(KRC_SOURCE_DIR) / "sysroot" / std::string(profile.name) /
            "include" / "bits";
        INFO("target: " << profile.name);
        CHECK(krc::target::generate_bits_types(profile) ==
              read_file(directory / "types.h"));
        CHECK(krc::target::generate_bits_stdint(profile) ==
              read_file(directory / "stdint.h"));
    }
}
