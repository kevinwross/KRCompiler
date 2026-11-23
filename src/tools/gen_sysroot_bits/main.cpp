#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "krc/target/sysroot_bits.h"
#include "krc/target/target.h"

namespace {

bool write_file(const std::filesystem::path& path, const std::string& contents) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        std::cerr << "gen_sysroot_bits: cannot create " << path.parent_path() << ": "
                  << error.message() << '\n';
        return false;
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        std::cerr << "gen_sysroot_bits: cannot open " << path << '\n';
        return false;
    }
    file << contents;
    if (!file) {
        std::cerr << "gen_sysroot_bits: cannot write " << path << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path output = "sysroot";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--out") {
            if (index + 1 >= argc) {
                std::cerr << "gen_sysroot_bits: --out requires a directory\n";
                return 2;
            }
            output = argv[++index];
        } else if (argument == "--help" || argument == "-h") {
            std::cout << "Usage: krc_gen_sysroot_bits [--out DIR]\n";
            return 0;
        } else {
            std::cerr << "gen_sysroot_bits: unknown option '" << argument << "'\n";
            return 2;
        }
    }

    for (const auto& profile : krc::target::target_profiles()) {
        const std::filesystem::path directory =
            output / std::string(profile.name) / "include" / "bits";
        if (!write_file(directory / "types.h", krc::target::generate_bits_types(profile)) ||
            !write_file(directory / "stdint.h",
                        krc::target::generate_bits_stdint(profile)) ||
            !write_file(directory / "float.h",
                        krc::target::generate_bits_float(profile))) {
            return 1;
        }
    }
    return 0;
}
