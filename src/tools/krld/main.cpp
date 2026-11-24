#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "krc/target/target.h"
#include "krc/toolchain.h"
#include "krc/version.h"
#include "linker/archive.h"
#include "linker/kro_object.h"

namespace toolchain = krc::toolchain;

namespace {

void print_usage(std::ostream& out) {
    out << "krld " << krc::kVersionString << " — KRCompiler linker\n"
        << "\n"
        << "Usage: krld [options] objects... -o image\n"
        << "\n"
        << "Options:\n"
        << "  --target=NAME          Target for .s inputs (kr32, kr16, kr64, kr216)\n"
        << "  --entry=NAME           Entry symbol (default _start)\n"
        << "  --base=ADDR            Image base (default 0x00010000)\n"
        << "  --flat                 Write the raw image instead of a container:\n"
        << "                         allocated sections at their addresses, no header\n"
        << "  -o FILE                Output executable\n"
        << "  --map=FILE             Write a deterministic image map\n"
        << "  --layout=FILE          Place sections using NAME ADDRESS lines\n"
        << "  --source-root=DIR      Emit debug source paths relative to DIR\n"
        << "  --print-size           Print section sizes\n"
        << "  --help                 Print this help\n";
}

std::uint64_t parse_address(std::string_view text, bool& ok) {
    ok = false;
    std::uint64_t value = 0;
    std::uint64_t base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text.remove_prefix(2);
    }
    if (text.empty()) {
        return 0;
    }
    for (const auto character : text) {
        unsigned digit = 0;
        if (character >= '0' && character <= '9') {
            digit = static_cast<unsigned>(character - '0');
        } else if (base == 16 && character >= 'a' && character <= 'f') {
            digit = static_cast<unsigned>(character - 'a' + 10);
        } else if (base == 16 && character >= 'A' && character <= 'F') {
            digit = static_cast<unsigned>(character - 'A' + 10);
        } else {
            return 0;
        }
        value = value * base + digit;
    }
    ok = true;
    return value;
}

bool read_binary_file(const std::string& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "krld: cannot open '" << path << "'\n";
        return false;
    }
    bytes.assign((std::istreambuf_iterator<char>(file)),
                 std::istreambuf_iterator<char>());
    if (file.bad()) {
        std::cerr << "krld: cannot read '" << path << "'\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string entry = "_start";
    std::uint64_t base = 0x00010000;
    bool base_set = false;
    std::string output;
    std::string map_path;
    std::string layout_path;
    std::string source_root;
    bool print_size = false;
    // `--flat` writes the image the way a boot loader reads it: no container
    // header, allocated sections at their linked addresses.
    bool flat = false;
    bool gc_sections = false;
    std::optional<krc::target::TargetId> options_target;
    std::vector<std::string> inputs;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--version") {
            std::cout << "krld " << krc::kVersionString << '\n';
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::cerr << "krld: -o requires a file\n";
                return 1;
            }
            output = argv[++index];
            continue;
        }
        if (argument.starts_with("--source-root=")) {
            source_root =
                std::string(argument.substr(std::string_view("--source-root=").size()));
            continue;
        }
        if (argument.starts_with("--target=")) {
            const auto name =
                argument.substr(std::string_view("--target=").size());
            const auto parsed = krc::target::parse_target(name);
            if (!parsed) {
                std::cerr << "krld: unknown target '" << name << "'\n";
                return 1;
            }
            options_target = *parsed;
            continue;
        }
        if (argument.starts_with("--entry=")) {
            entry = std::string(argument.substr(std::string_view("--entry=").size()));
            continue;
        }
        if (argument.starts_with("--map=")) {
            map_path = std::string(argument.substr(std::string_view("--map=").size()));
            continue;
        }
        if (argument.starts_with("--layout=")) {
            layout_path = std::string(argument.substr(std::string_view("--layout=").size()));
            continue;
        }
        if (argument == "--map") {
            if (index + 1 >= argc) {
                std::cerr << "krld: --map requires a file\n";
                return 1;
            }
            map_path = argv[++index];
            continue;
        }
        if (argument == "--flat") {
            flat = true;
            continue;
        }
        if (argument == "--print-size") {
            print_size = true;
            continue;
        }
        if (argument == "--gc-sections") {
            gc_sections = true;
            continue;
        }
        if (argument.starts_with("--base=")) {
            bool ok = false;
            base = parse_address(argument.substr(std::string_view("--base=").size()), ok);
            base_set = true;
            if (!ok) {
                std::cerr << "krld: invalid --base value\n";
                return 1;
            }
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "krld: unknown option '" << argument << "'\n";
            return 1;
        }
        inputs.emplace_back(argument);
    }

    if (inputs.empty() || output.empty()) {
        print_usage(std::cerr);
        return 1;
    }

    toolchain::Options options;
    options.target = toolchain::Target::Kr32;
    if (options_target) {
        switch (*options_target) {
        case krc::target::TargetId::Kr32: options.target = toolchain::Target::Kr32; break;
        case krc::target::TargetId::Kr16: options.target = toolchain::Target::Kr16; break;
        case krc::target::TargetId::Kr64: options.target = toolchain::Target::Kr64; break;
        case krc::target::TargetId::Kr216: options.target = toolchain::Target::Kr216; break;
        case krc::target::TargetId::Kr232: options.target = toolchain::Target::Kr232; break;
        }
    }
    options.base = base_set ? base : 0;
    options.entry = entry;
    options.gc_sections = gc_sections;
    options.source_root = source_root;

    std::vector<toolchain::Object> objects;
    for (const auto& path : inputs) {
        std::vector<std::uint8_t> bytes;
        if (!read_binary_file(path, bytes)) {
            return 1;
        }
        if (krc::linker::is_archive(bytes)) {
            std::vector<krc::linker::ArchiveMember> members;
            std::string error;
            if (!krc::linker::read_archive(bytes, members, error)) {
                std::cerr << "krld: " << path << ": " << error << '\n';
                return 1;
            }
            for (const auto& member : members) {
                objects.push_back(toolchain::Object{
                    path + "(" + member.name + ")",
                    std::vector<std::uint8_t>(member.data.begin(), member.data.end())});
            }
            continue;
        }
        const auto extension = std::filesystem::path(path).extension().string();
        if (extension == ".s" || extension == ".S") {
            const std::string text(bytes.begin(), bytes.end());
            const auto assembled = toolchain::assemble(text, options);
            if (!assembled.ok) {
                std::cerr << "krld: " << path << ": " << assembled.error << '\n';
                return 1;
            }
            objects.push_back(toolchain::Object{path, assembled.object});
            continue;
        }
        objects.push_back(toolchain::Object{path, std::move(bytes)});
    }

    if (!layout_path.empty()) {
        std::ifstream layout(layout_path);
        if (!layout) {
            std::cerr << "krld: cannot open layout '" << layout_path << "'\n";
            return 1;
        }
        std::string line;
        while (std::getline(layout, line)) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            std::istringstream stream(line);
            std::string name;
            std::string address_text;
            if (!(stream >> name >> address_text)) {
                std::cerr << "krld: invalid layout line '" << line << "'\n";
                return 1;
            }
            bool ok = false;
            const auto address = parse_address(address_text, ok);
            if (!ok) {
                std::cerr << "krld: invalid layout address '" << address_text << "'\n";
                return 1;
            }
            options.section_addresses.push_back({name, address});
        }
    }

    const auto linked = toolchain::link(objects, options);
    if (!linked.ok) {
        std::cerr << "krld: " << linked.error << '\n';
        return 1;
    }

    if (print_size) {
        for (const auto& section : linked.sections) {
            std::cout << section.name << " 0x" << std::hex << section.address << std::dec
                      << " size=" << section.size << " align=" << section.alignment << '\n';
        }
    }
    if (!map_path.empty()) {
        std::ofstream map(map_path);
        if (!map) {
            std::cerr << "krld: cannot write map '" << map_path << "'\n";
            return 1;
        }
        map << "base=0x" << std::hex << linked.base << " entry=0x" << linked.entry
            << std::dec << '\n';
        map << "section address size\n";
        for (const auto& section : linked.sections) {
            map << section.name << " 0x" << std::hex << section.address << std::dec
                << ' ' << section.size << '\n';
        }
        map << "symbol value size binding\n";
        for (const auto& symbol : linked.symbols) {
            map << symbol.name << " 0x" << std::hex << symbol.value << std::dec << ' '
                << symbol.size << ' ' << (symbol.global ? "global" : "local") << '\n';
        }
    }

    std::ofstream file(output, std::ios::binary);
    if (!file) {
        std::cerr << "krld: cannot open output '" << output << "'\n";
        return 1;
    }
    const auto bytes = flat ? krc::toolchain::flatten(linked) : linked.image;
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        std::cerr << "krld: failed to write '" << output << "'\n";
        return 1;
    }
    std::cout << "krld: linked " << objects.size() << " object(s) to " << output
              << " (entry " << linked.entry << ")\n";
    return 0;
}
