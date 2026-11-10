#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "asm/kro_writer.h"
#include "asm/kr32_assembler.h"
#include "krc/target/target.h"
#include "krc/version.h"
#include "linker/archive.h"
#include "linker/kro_object.h"
#include "linker/linker.h"

namespace {

void print_usage(std::ostream& out) {
    out << "krld " << krc::kVersionString << " — KRCompiler linker\n"
        << "\n"
        << "Usage: krld [options] objects... -o image\n"
        << "\n"
        << "Options:\n"
        << "  --entry=NAME           Entry symbol (default _start)\n"
        << "  --base=ADDR            Image base (default 0x00010000)\n"
        << "  -o FILE                Output executable\n"
        << "  --map=FILE             Write a deterministic image map\n"
        << "  --layout=FILE          Place sections using NAME ADDRESS lines\n"
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

}  // namespace

int main(int argc, char** argv) {
    std::string entry = "_start";
    std::uint64_t base = 0x00010000;
    std::string output;
    std::string map_path;
    std::string layout_path;
    bool print_size = false;
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
        if (argument == "--print-size") {
            print_size = true;
            continue;
        }
        if (argument.starts_with("--base=")) {
            bool ok = false;
            base = parse_address(argument.substr(std::string_view("--base=").size()), ok);
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

    std::vector<krc::linker::KroObject> objects;
    for (const auto& path : inputs) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cerr << "krld: cannot open '" << path << "'\n";
            return 1;
        }
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                        std::istreambuf_iterator<char>());
        krc::linker::KroObject object;
        std::string error;
        const auto extension = std::filesystem::path(path).extension().string();
        if (krc::linker::is_archive(bytes)) {
            std::vector<krc::linker::ArchiveMember> members;
            if (!krc::linker::read_archive(bytes, members, error)) {
                std::cerr << "krld: " << path << ": " << error << '\n';
                return 1;
            }
            for (const auto& member : members) {
                krc::linker::KroObject member_object;
                if (!krc::linker::read_object(member.data, member_object, error)) {
                    std::cerr << "krld: " << path << "(" << member.name << "): " << error
                              << '\n';
                    return 1;
                }
                objects.push_back(std::move(member_object));
            }
            continue;
        }
        if (extension == ".s" || extension == ".S") {
            const std::string source(bytes.begin(), bytes.end());
            krc::asmkr32::AssemblyResult assembly;
            if (!krc::asmkr32::assemble(source, assembly, error)) {
                std::cerr << "krld: " << path << ": " << error << '\n';
                return 1;
            }
            const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
            if (!krc::asmkr32::make_object(assembly, profile, object, error)) {
                std::cerr << "krld: " << path << ": " << error << '\n';
                return 1;
            }
        } else if (!krc::linker::read_object(bytes, object, error)) {
            std::cerr << "krld: " << path << ": " << error << '\n';
            return 1;
        }
        objects.push_back(std::move(object));
    }

    krc::linker::LinkOptions options;
    options.base = base;
    options.entry_symbol = entry;
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
            options.section_addresses[name] = address;
        }
    }
    krc::linker::LinkedImage image;
    std::string error;
    if (!krc::linker::link_objects(objects, options, image, error)) {
        std::cerr << "krld: " << error << '\n';
        return 1;
    }
    if (!krc::linker::attach_kdbg(image, error)) {
        std::cerr << "krld: " << error << '\n';
        return 1;
    }

    if (print_size) {
        for (const auto& section : image.sections) {
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
        map << "base=0x" << std::hex << image.base << " entry=0x" << image.entry
            << std::dec << '\n';
        map << "section address size\n";
        for (const auto& section : image.sections) {
            map << section.name << " 0x" << std::hex << section.address << std::dec
                << ' ' << section.size << '\n';
        }
        map << "symbol value size binding\n";
        for (const auto& symbol : image.symbols) {
            map << symbol.name << " 0x" << std::hex << symbol.value << std::dec << ' '
                << symbol.size << ' '
                << (symbol.binding == krc::linker::KroSymbolBinding::Global ? "global"
                                                                            : "local")
                << '\n';
        }
    }

    std::vector<std::uint8_t> executable;
    if (!krc::linker::write_kr32_executable(image, executable, error)) {
        std::cerr << "krld: " << error << '\n';
        return 1;
    }
    std::ofstream file(output, std::ios::binary);
    if (!file) {
        std::cerr << "krld: cannot open output '" << output << "'\n";
        return 1;
    }
    file.write(reinterpret_cast<const char*>(executable.data()),
               static_cast<std::streamsize>(executable.size()));
    if (!file) {
        std::cerr << "krld: failed to write '" << output << "'\n";
        return 1;
    }
    std::cout << "krld: linked " << objects.size() << " object(s) to " << output
              << " (entry " << image.entry << ")\n";
    return 0;
}
