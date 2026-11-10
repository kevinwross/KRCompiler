#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "krc/tools/fixture_extractor.h"

namespace {

struct ManifestRow {
    std::string source;
    std::uint32_t line = 0;
    std::string output;
    std::size_t bytes = 0;
};

std::string read_file(const std::filesystem::path& path, bool& ok) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ok = false;
        return {};
    }
    std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    ok = !input.bad();
    return text;
}

std::string extension_for(const std::filesystem::path& path) {
    const auto name = path.filename().string();
    if (name.find("test_cc_") != std::string::npos ||
        path.parent_path().filename() == "cc") {
        return ".c";
    }
    if (name.find("test_cxx_") != std::string::npos ||
        path.parent_path().filename() == "cxx") {
        return ".cpp";
    }
    return ".cpp";
}

void print_usage(std::ostream& out) {
    out << "Usage: krc_extract_fixtures [--out DIR] [--limit N] files...\n"
        << "\n"
        << "Extracts C/C++ source from compile/compile_and_run calls in doctest files.\n"
        << "Default output: tests/fixtures/extracted\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::filesystem::path output = "tests/fixtures/extracted";
    std::size_t limit = 0;
    std::vector<std::filesystem::path> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--out") {
            if (i + 1 >= argc) {
                std::cerr << "krc_extract_fixtures: --out requires a directory\n";
                return 2;
            }
            output = argv[++i];
            continue;
        }
        if (argument.starts_with("--out=")) {
            output = std::string(argument.substr(std::string_view("--out=").size()));
            continue;
        }
        if (argument == "--limit") {
            if (i + 1 >= argc) {
                std::cerr << "krc_extract_fixtures: --limit requires a count\n";
                return 2;
            }
            try {
                limit = static_cast<std::size_t>(std::stoull(argv[++i]));
            } catch (const std::exception&) {
                std::cerr << "krc_extract_fixtures: invalid --limit value\n";
                return 2;
            }
            continue;
        }
        if (argument.starts_with("-")) {
            std::cerr << "krc_extract_fixtures: unknown option '" << argument << "'\n";
            return 2;
        }
        inputs.emplace_back(argument);
    }

    if (inputs.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) {
        std::cerr << "krc_extract_fixtures: cannot create '" << output.string()
                  << "': " << error.message() << '\n';
        return 1;
    }

    std::vector<ManifestRow> manifest;
    std::size_t extracted = 0;

    for (const auto& input : inputs) {
        bool ok = false;
        const auto source = read_file(input, ok);
        if (!ok) {
            std::cerr << "krc_extract_fixtures: cannot read '" << input.string() << "'\n";
            return 1;
        }

        auto fixtures = krc::tools::extract_fixtures(source, input.string());
        if (limit != 0 && fixtures.size() > limit) {
            fixtures.resize(limit);
        }

        const auto stem = input.filename().stem().string();
        const auto extension = extension_for(input);
        for (std::size_t index = 0; index < fixtures.size(); ++index) {
            const auto& fixture = fixtures[index];
            const auto relative = std::filesystem::path(stem) /
                                  (stem + "_" + std::to_string(index) + extension);
            const auto destination = output / relative;
            std::filesystem::create_directories(destination.parent_path(), error);
            if (error) {
                std::cerr << "krc_extract_fixtures: cannot create output directory\n";
                return 1;
            }
            std::ofstream file(destination, std::ios::binary | std::ios::trunc);
            if (!file) {
                std::cerr << "krc_extract_fixtures: cannot write '" << destination.string()
                          << "'\n";
                return 1;
            }
            file << fixture.text;
            if (!file) {
                std::cerr << "krc_extract_fixtures: write failed for '" << destination.string()
                          << "'\n";
                return 1;
            }
            manifest.push_back(ManifestRow{fixture.source_name, fixture.line,
                                           relative.generic_string(), fixture.text.size()});
            ++extracted;
        }
    }

    std::ofstream manifest_file(output / "manifest.tsv", std::ios::binary | std::ios::trunc);
    if (!manifest_file) {
        std::cerr << "krc_extract_fixtures: cannot write manifest\n";
        return 1;
    }
    manifest_file << "source\tline\toutput\tbytes\n";
    for (const auto& row : manifest) {
        manifest_file << row.source << '\t' << row.line << '\t' << row.output << '\t' << row.bytes
                      << '\n';
    }
    if (!manifest_file) {
        std::cerr << "krc_extract_fixtures: manifest write failed\n";
        return 1;
    }

    std::cout << "extracted " << extracted << " fixtures from " << inputs.size()
              << " file(s) into " << output.string() << '\n';
    return 0;
}
