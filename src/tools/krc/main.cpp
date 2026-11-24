#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "krc/debug/kdbg.h"
#include "krc/image/k232.h"
#include "krc/target/target.h"
#include "krc/toolchain.h"
#include "krc/version.h"

#ifndef KRC_SYSROOT_DIR
#define KRC_SYSROOT_DIR "sysroot"
#endif

namespace frontend = krc::frontend;
namespace toolchain = krc::toolchain;

namespace {

void print_usage(std::ostream& out) {
    out << "krc " << krc::kVersionString << " — KRCompiler driver\n"
        << "\n"
        << "Usage: krc [options] files...\n"
        << "       krc image [options] objects... -o image\n"
        << "\n"
        << "Options:\n"
        << "  --version              Print the compiler version\n"
        << "  --help                 Print this help\n"
        << "  --list-targets         List target profiles\n"
        << "  --target=NAME          Select kr32, kr16, or kr64\n"
        << "  -std=c11               Select the C dialect (C++ starts later)\n"
        << "  -fsyntax-only          Preprocess, parse, and check only\n"
        << "  -E                     Preprocess only\n"
        << "  --dump-ast             Print the parsed C AST\n"
        << "  --dump-ir              Lower to typed SSA IR and print it\n"
        << "  --dump-stats           Print IR instruction and block counts per function\n"
        << "  -O0                    Straightforward lowering (the default)\n"
        << "  -O1                    The optimiser passes plan.md 8.8 lists\n"
        << "  --dump-symbols FILE    Dump KDBG symbols from a .kr32 or .kr16 image\n"
        << "  --json                 Use JSON with --dump-symbols\n"
        << "  -S                     Emit KR32 assembly to stdout, or to -o FILE\n"
        << "  -c                     Emit a relocatable KRO object\n"
        << "  --emit-object          Write a KR16 KRO object instead of .kr16\n"
        << "  -o FILE                Output path for -S or -c\n"
        << "  -I DIR                 Add an include search directory\n"
        << "  --sysroot DIR          Use DIR as the target sysroot\n"
        << "\n"
        << "KR32 code generation is complete for the implemented C and C++ subset;\n"
        << "KR16 compiles C to a .kr16 image or a KRO object, and KR64 has no backend.\n";
}

int fail(std::string_view message) {
    std::cerr << "krc: error: " << message << '\n';
    return 2;
}

void add_include_directory(std::vector<std::string>& paths,
                           const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error) && !error) {
        paths.push_back(path.string());
    }
}

bool read_text_file(const std::string& path, std::string& text, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "unable to open '" + path + "'";
        return false;
    }
    text.assign((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = "unable to read '" + path + "'";
        return false;
    }
    return true;
}

bool read_binary_file(const std::string& path, std::vector<std::uint8_t>& bytes,
                      std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path + "'";
        return false;
    }
    bytes.assign((std::istreambuf_iterator<char>(file)),
                 std::istreambuf_iterator<char>());
    if (file.bad()) {
        error = "cannot read '" + path + "'";
        return false;
    }
    return true;
}

bool write_text_file(const std::filesystem::path& path, std::string_view text,
                     std::string& error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        error = "failed to write '" + path.string() + "'";
        return false;
    }
    return true;
}

bool write_binary_file(const std::filesystem::path& path,
                       const std::vector<std::uint8_t>& bytes, std::string& error) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        error = "cannot open '" + path.string() + "'";
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        error = "failed to write '" + path.string() + "'";
        return false;
    }
    return true;
}

toolchain::Target to_target(krc::target::TargetId id) {
    switch (id) {
    case krc::target::TargetId::Kr16:
        return toolchain::Target::Kr16;
    case krc::target::TargetId::Kr64:
        return toolchain::Target::Kr64;
    case krc::target::TargetId::Kr216:
        return toolchain::Target::Kr216;
    case krc::target::TargetId::Kr232:
        return toolchain::Target::Kr232;
    case krc::target::TargetId::Kr32:
        break;
    }
    return toolchain::Target::Kr32;
}

// The stub a bare `krc -c --target=kr16` prepends so the image has an entry and
// a way to report output without a separate runtime link. It is part of the
// CLI's convenience form, not of the facade.
constexpr std::string_view kKr16EntryStub =
    ".text\n.global _start\n_start:\n"
    "    CALL main\n"
    "    HLT\n"
    ".global puts\nputs:\n"
    "    MOV R2, #0xF000\n"
    ".Lputs_loop:\n"
    "    LDB R0, [R1]\n"
    "    CMP R0, #0\n"
    "    BEQ .Lputs_done\n"
    "    STB [R2], R0\n"
    "    ADD R1, #1\n"
    "    BRA .Lputs_loop\n"
    ".Lputs_done:\n"
    "    MOV R0, #0\n"
    "    RET\n";

toolchain::Diagnostic error_diagnostic(std::string message) {
    toolchain::Diagnostic diagnostic;
    diagnostic.severity = krc::frontend::Severity::Error;
    diagnostic.message = std::move(message);
    return diagnostic;
}

}  // namespace

namespace {

// `krc image` builds one image from objects and assembly sources: the command
// the guest-artefact pipeline calls. It shares the linker, the flat writer and
// the map renderer with `krld`, so the two cannot disagree about what an image
// is; only the command line differs.
int run_image(int argc, char** argv) {
    std::string target_name = "kr32";
    std::string entry = "_start";
    std::string output_path;
    std::string map_path;
    std::string sym_path;
    std::vector<std::string> inputs;
    bool flat = false;
    std::uint64_t base = 0;
    bool base_set = false;

    auto parse_address = [](std::string_view text, std::uint64_t& value) {
        try {
            std::size_t used = 0;
            const std::string owned(text);
            value = std::stoull(owned, &used, 0);
            return used == owned.size();
        } catch (...) {
            return false;
        }
    };

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            std::cout << "usage: krc image [options] objects... -o image\n"
                         "  --target=NAME   kr32, kr16, kr64, kr216 or kr232 (default kr32)\n"
                         "  --base=ADDR     Image base\n"
                         "  --entry=NAME    Entry symbol (default _start)\n"
                         "  --flat          Write the raw image instead of a container\n"
                         "  --map=FILE      Write the deterministic image map\n"
                         "  --sym=FILE      Write the kr32 symbol sidecar\n"
                         "  -o FILE         Output image\n";
            return 0;
        }
        if (argument == "-o") {
            if (index + 1 >= argc) {
                std::cerr << "krc image: -o requires a file\n";
                return 1;
            }
            output_path = argv[++index];
            continue;
        }
        if (argument == "--flat") {
            flat = true;
            continue;
        }
        if (argument.starts_with("--target=")) {
            target_name = std::string(argument.substr(std::string_view("--target=").size()));
            continue;
        }
        if (argument.starts_with("--entry=")) {
            entry = std::string(argument.substr(std::string_view("--entry=").size()));
            continue;
        }
        if (argument == "--map") {
            if (index + 1 >= argc) {
                std::cerr << "krc image: --map requires a file\n";
                return 1;
            }
            map_path = argv[++index];
            continue;
        }
        if (argument.starts_with("--map=")) {
            map_path = std::string(argument.substr(std::string_view("--map=").size()));
            continue;
        }
        if (argument == "--sym") {
            if (index + 1 >= argc) {
                std::cerr << "krc image: --sym requires a file\n";
                return 1;
            }
            sym_path = argv[++index];
            continue;
        }
        if (argument.starts_with("--sym=")) {
            sym_path = std::string(argument.substr(std::string_view("--sym=").size()));
            continue;
        }
        if (argument.starts_with("--base=")) {
            if (!parse_address(argument.substr(std::string_view("--base=").size()), base)) {
                std::cerr << "krc image: invalid --base value\n";
                return 1;
            }
            base_set = true;
            continue;
        }
        if (argument.starts_with("--")) {
            std::cerr << "krc image: unknown option '" << argument << "'\n";
            return 1;
        }
        inputs.emplace_back(argument);
    }

    if (inputs.empty() || output_path.empty()) {
        std::cerr << "krc image: inputs and -o are required\n";
        return 1;
    }
    const auto parsed = krc::target::parse_target(target_name);
    if (!parsed) {
        std::cerr << "krc image: unknown target '" << target_name << "'\n";
        return 1;
    }

    // G4: the writer suggests the convention. A -o with no suffix gets the
    // target's image extension -- or .flat for raw output -- and a suffix that
    // names a different target is refused, naming both, because it contradicts
    // --target rather than merely being unusual.
    {
        const std::filesystem::path requested(output_path);
        const std::string suffix = requested.extension().string();
        const std::string wanted =
            flat ? std::string(".flat")
                 : std::string(krc::target::image_extension(*parsed));
        if (suffix.empty()) {
            output_path =
                (requested.parent_path() / (requested.filename().string() + wanted)).string();
        } else if (const auto claimed = krc::target::target_for_extension(suffix);
                   claimed.has_value() && *claimed != *parsed) {
            std::cerr << "krc image: '" << output_path << "' says "
                      << krc::target::get_target(*claimed).name << " but --target is "
                      << krc::target::get_target(*parsed).name << '\n';
            return 1;
        }
    }
    toolchain::Options options;
    options.target = to_target(*parsed);
    options.entry = entry;
    options.base = base_set ? base : 0;
    options.symbol_sidecar = !sym_path.empty();

    std::vector<toolchain::Object> objects;
    for (const auto& path : inputs) {
        std::ifstream input(path, std::ios::binary);
        if (!input) {
            std::cerr << "krc image: cannot read '" << path << "'\n";
            return 1;
        }
        std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(input),
                                        std::istreambuf_iterator<char>()};
        // The content decides, not the suffix (G7): an explicit path names a
        // file, and an object called program.s must still link.
        if (!toolchain::is_relocatable_object(bytes)) {
            const std::string text(bytes.begin(), bytes.end());
            const auto assembled = toolchain::assemble(text, options);
            if (!assembled.ok) {
                std::cerr << "krc image: " << path << ": " << assembled.error << '\n';
                return 1;
            }
            objects.push_back(toolchain::Object{path, assembled.object});
            continue;
        }
        objects.push_back(toolchain::Object{path, std::move(bytes)});
    }

    const auto linked = toolchain::link(objects, options);
    if (!linked.ok) {
        std::cerr << "krc image: " << linked.error << '\n';
        return 1;
    }

    const auto bytes = flat ? toolchain::flatten(linked) : linked.image;
    std::ofstream image(output_path, std::ios::binary);
    if (!image) {
        std::cerr << "krc image: cannot write '" << output_path << "'\n";
        return 1;
    }
    image.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    if (!image) {
        std::cerr << "krc image: failed to write '" << output_path << "'\n";
        return 1;
    }
    if (!map_path.empty()) {
        std::ofstream map(map_path);
        map << toolchain::map_text(linked);
        if (!map) {
            std::cerr << "krc image: cannot write map '" << map_path << "'\n";
            return 1;
        }
    }
    if (!sym_path.empty()) {
        if (linked.sidecar.empty()) {
            std::cerr << "krc image: --sym is defined for kr32 images; this target wrote none\n";
            return 1;
        }
        std::ofstream sidecar(sym_path, std::ios::binary);
        sidecar.write(reinterpret_cast<const char*>(linked.sidecar.data()),
                      static_cast<std::streamsize>(linked.sidecar.size()));
        if (!sidecar) {
            std::cerr << "krc image: cannot write sidecar '" << sym_path << "'\n";
            return 1;
        }
    }
    std::cout << "krc image: linked " << objects.size() << " object(s) to " << output_path
              << " (entry " << linked.entry << ")\n";
    return 0;
}

} // namespace
int main(int argc, char** argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "image") {
        return run_image(argc - 1, argv + 1);
    }
    std::string target_name = "kr32";
    std::vector<std::string> inputs;
    std::vector<std::string> include_paths;
    bool syntax_only = false;
    bool preprocess_only = false;
    bool dump_ast = false;
    bool dump_ir = false;
    bool dump_stats = false;
    int optimization = 0;
    bool emit_assembly = false;
    bool emit_object = false;
    bool emit_object_container = false;
    bool far_calls = false;
    bool dump_symbols = false;
    bool json_output = false;
    bool cxx_mode = false;
    std::string output_path;
    std::string sysroot_path = KRC_SYSROOT_DIR;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        if (argument == "--version") {
            std::cout << "krc " << krc::kVersionString << '\n'
                  << "build " << krc::kBuildType << ", revision " << krc::kBuildRevision
                  << ", " << krc::kBuildCompiler << '\n';
            return 0;
        }
        if (argument == "--help" || argument == "-h") {
            print_usage(std::cout);
            return 0;
        }
        if (argument == "--list-targets") {
            for (const auto& profile : krc::target::target_profiles()) {
                std::cout << profile.name << "  " << krc::target::to_string(profile.family)
                          << "  pointer=" << profile.pointer_bits
                          << "  data=" << krc::target::to_string(profile.data_model)
                          << "  backend=" << (profile.backend_available ? "yes" : "no") << '\n';
            }
            return 0;
        }
        if (argument.starts_with("--target=")) {
            const auto value = argument.substr(std::string_view("--target=").size());
            if (value.empty()) {
                return fail("--target requires a value");
            }
            target_name = std::string(value);
            continue;
        }
        if (argument == "--target") {
            if (i + 1 >= argc) {
                return fail("--target requires a value");
            }
            target_name = argv[++i];
            continue;
        }
        if (argument.starts_with("-std=")) {
            const auto value = argument.substr(std::string_view("-std=").size());
            if (value == "c++11" || value == "c++14" || value == "c++17") {
                cxx_mode = true;
                continue;
            }
            if (value != "c11") {
                return fail("only -std=c11 and the C++ subset are supported");
            }
            continue;
        }
        if (argument == "-fsyntax-only") {
            syntax_only = true;
            continue;
        }
        if (argument == "-E") {
            preprocess_only = true;
            continue;
        }
        if (argument == "--dump-ast") {
            dump_ast = true;
            continue;
        }
        if (argument == "--dump-ir") {
            dump_ir = true;
            continue;
        }
        if (argument == "--dump-stats") {
            dump_stats = true;
            continue;
        }
        // `-O0` is straightforward lowering; `-O1` adds the optimiser passes
        // plan.md §8.8 lists. The level is not inferred from anywhere else, so
        // a build without the flag is `-O0` and stays byte-identical to one that
        // names it.
        if (argument == "-O0") {
            optimization = 0;
            continue;
        }
        if (argument == "-O1" || argument == "-O") {
            optimization = 1;
            continue;
        }
        if (argument == "--dump-symbols") {
            dump_symbols = true;
            continue;
        }
        if (argument == "--json") {
            json_output = true;
            continue;
        }
        if (argument == "-S") {
            emit_assembly = true;
            continue;
        }
        if (argument == "-I") {
            if (i + 1 >= argc) {
                return fail("-I requires a directory");
            }
            include_paths.emplace_back(argv[++i]);
            continue;
        }
        if (argument.starts_with("-I") && argument.size() > 2) {
            include_paths.emplace_back(std::string(argument.substr(2)));
            continue;
        }
        if (argument == "--sysroot") {
            if (i + 1 >= argc) {
                return fail("--sysroot requires a directory");
            }
            sysroot_path = argv[++i];
            continue;
        }
        if (argument.starts_with("--sysroot=") && argument.size() > 10) {
            sysroot_path = std::string(argument.substr(10));
            continue;
        }
        if (argument == "-c") {
            emit_object = true;
            continue;
        }
        if (argument == "--far-calls") {
            far_calls = true;
            continue;
        }
        if (argument == "--emit-object") {
            emit_object_container = true;
            continue;
        }
        if (argument == "-o") {
            if (i + 1 >= argc) {
                return fail("-o requires a file");
            }
            output_path = argv[++i];
            continue;
        }
        if (!argument.empty() && argument.front() == '-') {
            return fail("unknown option '" + std::string(argument) + "'");
        }
        inputs.emplace_back(argument);
    }

    const auto target_id = krc::target::parse_target(target_name);
    if (!target_id) {
        return fail("unknown target '" + target_name + "' (expected kr32, kr16, or kr64)");
    }
    const auto& profile = krc::target::get_target(*target_id);

    // `--dump-symbols` takes the image as an ordinary input, so the documented
    // `krc --dump-symbols --json IMAGE` order works as well as `--dump-symbols
    // IMAGE --json`.
    if (inputs.empty() && !dump_symbols) {
        print_usage(std::cout);
        return 0;
    }

    for (const auto& input : inputs) {
        const auto extension = std::filesystem::path(input).extension().string();
        if (extension == ".cpp" || extension == ".cc" || extension == ".cxx") {
            cxx_mode = true;
        }
    }

    // -S and -c each write their whole output to -o, so one path cannot hold
    // both: the object would overwrite the assembly it was assembled from.
    if (emit_assembly && emit_object && !output_path.empty()) {
        return fail("-S and -c both write to -o; run them separately");
    }

    if (dump_symbols) {
        if (inputs.empty()) {
            return fail("--dump-symbols requires a file");
        }
        if (inputs.size() != 1) {
            return fail("--dump-symbols accepts exactly one image");
        }
        std::vector<std::uint8_t> bytes;
        std::string error;
        if (!read_binary_file(inputs.front(), bytes, error)) {
            return fail(error);
        }
        krc::debug::KdbgImage image;
        // A `.kr232` container is not a KDBG image: its debug payload is the
        // `.kdbg` section, which is decoded directly.
        const bool is_container = bytes.size() >= 4 && bytes[0] == 'K' && bytes[1] == '2' &&
                                  bytes[2] == '3' && bytes[3] == '2';
        if (is_container) {
            std::span<const std::uint8_t> payload;
            if (!krc::image::read_k232_section(bytes, ".kdbg", payload, error)) {
                return fail(error);
            }
            if (!krc::debug::decode_kdbg(payload, image, error)) {
                return fail(error);
            }
        } else if (!krc::debug::decode_kdbg_image(bytes, image, error)) {
            return fail(error);
        }
        // A known suffix that contradicts the payload is refused and both values
        // are named, because that is what catches a renamed or mixed-up image. An
        // unknown suffix names nothing and is accepted: program.kr32.backup is a
        // person being deliberate, not an error.
        const auto suffix = std::filesystem::path(inputs.front()).extension().string();
        if (const auto named = krc::target::target_for_extension(suffix)) {
            // Container and KDBG target ids are 1-5 in the order KR32, KR16, KR64,
            // KR216, KR232 (ADR 0057); the enum carries the same order.
            const auto expected = static_cast<std::uint32_t>(*named) + 1;
            if (image.target_id != expected) {
                return fail(std::string("the file name says ") + suffix +
                            " but the image carries target " +
                            std::to_string(image.target_id) + ", so it is a " +
                            std::string(krc::target::to_string(*named)) +
                            " image only by name");
            }
        }
        std::cout << (json_output ? krc::debug::dump_kdbg_json(image)
                                  : krc::debug::dump_kdbg_text(image));
        return 0;
    }

    const std::filesystem::path sysroot(sysroot_path);
    add_include_directory(include_paths, sysroot / "include");
    add_include_directory(include_paths, sysroot / std::string(profile.name) / "include");

    toolchain::Options options;
    options.target = to_target(*target_id);
    options.cxx = cxx_mode;
    options.optimize = optimization;
    options.include_paths = include_paths;
    if (emit_assembly || emit_object) {
        options.stage = toolchain::Stage::Assembly;
    } else if (dump_ir || dump_stats) {
        options.stage = toolchain::Stage::Ir;
    } else {
        options.stage = toolchain::Stage::Syntax;
    }
    options.dump_ast = dump_ast;
    options.dump_ir = dump_ir;
    options.dump_stats = dump_stats;
    // A bare `krc --target=kr216 file.c` is a request to produce something, like
    // the other targets' default paths: assembly on stdout, or an object when -o
    // names a file. The old fall-through printed the front-end-only message after
    // the backend existed.
    if (target_id == krc::target::TargetId::Kr216 && options.stage == toolchain::Stage::Syntax &&
        !syntax_only && !preprocess_only && !dump_ast && !dump_ir && !dump_stats) {
        options.stage = toolchain::Stage::Assembly;
        if (output_path.empty()) {
            emit_assembly = true;
        } else {
            emit_object = true;
        }
    }

    toolchain::Options link_options = options;
    if (target_id == krc::target::TargetId::Kr16 ||
        target_id == krc::target::TargetId::Kr216) {
        link_options.base = 0x100;
        link_options.entry = "_start";
    } else if (target_id == krc::target::TargetId::Kr232) {
        link_options.base = 0x200;  // above the reset word and the vectors
        link_options.entry = "main";
    }

    std::vector<toolchain::Diagnostic> diagnostics;
    bool failed = false;

    for (const auto& input : inputs) {
        const auto input_extension = std::filesystem::path(input).extension().string();
        if ((emit_object_container || emit_object) && input_extension == ".s") {
            if (target_id != krc::target::TargetId::Kr16 &&
                target_id != krc::target::TargetId::Kr216 &&
                target_id != krc::target::TargetId::Kr232) {
                diagnostics.push_back(error_diagnostic(
                    "target '" + std::string(profile.name) +
                    "' does not accept assembly input for -c"));
                failed = true;
                continue;
            }
            std::string text;
            std::string error;
            if (!read_text_file(input, text, error)) {
                diagnostics.push_back(error_diagnostic(error));
                failed = true;
                continue;
            }
            toolchain::Options assemble_options = options;
            assemble_options.far_calls = far_calls;
            assemble_options.target = to_target(*target_id);
            assemble_options.source_name = input;
            const auto assembled =
                toolchain::assemble(text, assemble_options);
            if (!assembled.ok) {
                diagnostics.push_back(error_diagnostic(assembled.error));
                failed = true;
                continue;
            }
            const auto path = output_path.empty()
                                  ? std::filesystem::path(input).replace_extension(".kro")
                                  : std::filesystem::path(output_path);
            if (!write_binary_file(path, assembled.object, error)) {
                diagnostics.push_back(error_diagnostic(error));
                failed = true;
            }
            continue;
        }

        if (preprocess_only) {
            std::string text;
            std::string error;
            if (!read_text_file(input, text, error)) {
                diagnostics.push_back(error_diagnostic(error));
                failed = true;
                continue;
            }
            const toolchain::Source source{input, text};
            const auto preprocessed = toolchain::preprocess(source, options);
            diagnostics.insert(diagnostics.end(), preprocessed.diagnostics.begin(),
                               preprocessed.diagnostics.end());
            if (!preprocessed.ok) {
                failed = true;
                continue;
            }
            bool first = true;
            for (const auto& token : preprocessed.tokens) {
                if (!first) {
                    std::cout << ' ';
                }
                std::cout << token.text;
                first = false;
            }
            std::cout << '\n';
            continue;
        }

        std::string text;
        std::string error;
        if (!read_text_file(input, text, error)) {
            diagnostics.push_back(error_diagnostic(error));
            failed = true;
            continue;
        }
        toolchain::Options file_options = options;
        file_options.source_name = input;
        file_options.far_calls = far_calls;
        const toolchain::Source source{input, text};
        const auto compiled = toolchain::compile(source, file_options);
        diagnostics.insert(diagnostics.end(), compiled.diagnostics.begin(),
                           compiled.diagnostics.end());
        // A partial AST is dumped even when parsing reported errors, matching the
        // drift the compiler has always allowed: the dump is what shows the
        // reader where the parser stopped.
        if (dump_ast) {
            std::cout << compiled.ast_dump;
        }
        if (dump_ir) {
            std::cout << compiled.ir_dump;
        }
        if (dump_stats) {
            std::cout << compiled.stats;
        }
        if (!compiled.ok) {
            failed = true;
            continue;
        }
        if (!emit_assembly && !emit_object) {
            continue;
        }

        // G2: an object is named for the target it was assembled for --
        // prog.kr32.kro -- because one source is routinely built for several
        // targets in one directory. Images keep their single-extension names.
        const auto emit_path = [&](std::string_view extension) {
            if (!output_path.empty()) {
                return std::filesystem::path(output_path);
            }
            if (extension == ".kro") {
                const std::string target_label(
                    krc::target::get_target(*target_id).name);
                return std::filesystem::path(input).replace_extension(
                    "." + target_label + ".kro");
            }
            return std::filesystem::path(input).replace_extension(extension);
        };

        // For KR16, `-c` owns the output: the old driver printed the assembly
        // only in the branch that ran when no object was requested.
        const bool suppress_assembly = target_id == krc::target::TargetId::Kr16 &&
                                       emit_object;
        if (emit_assembly && !suppress_assembly) {
            if (output_path.empty()) {
                // G8: generated assembly is a file named for its target, so
                // prog.c produces prog.kr32.s rather than a stream a caller has
                // to redirect. An explicit -o still wins.
                const std::string target_label(
                    krc::target::get_target(*target_id).name);
                const std::filesystem::path generated =
                    std::filesystem::path(std::filesystem::path(input).filename())
                        .replace_extension("." + target_label + ".s");
                if (!write_text_file(generated, compiled.assembly, error)) {
                    diagnostics.push_back(
                        error_diagnostic("cannot open assembly output '" +
                                         generated.string() + "'"));
                    failed = true;
                }
            } else if (!write_text_file(output_path, compiled.assembly, error)) {
                diagnostics.push_back(
                    error_diagnostic("cannot open assembly output '" +
                                     std::string(output_path) + "'"));
                failed = true;
            }
        }

        if (target_id == krc::target::TargetId::Kr16) {
            if (emit_object_container) {
                const auto assembled = toolchain::assemble(
                    compiled.assembly, compiled.debug_info, options);
                if (!assembled.ok) {
                    diagnostics.push_back(error_diagnostic(assembled.error));
                    failed = true;
                    continue;
                }
                if (!write_binary_file(emit_path(".kro"), assembled.object, error)) {
                    diagnostics.push_back(error_diagnostic(error));
                    failed = true;
                }
                continue;
            }
            if (emit_object) {
                std::string stub(kKr16EntryStub);
                stub += compiled.assembly;
                // The bridge names labels in the user assembly; the stub adds
                // none, so passing it through is what makes the image decodable.
                const auto image_object =
                    toolchain::assemble(stub, compiled.debug_info, options);
                if (!image_object.ok) {
                    diagnostics.push_back(error_diagnostic(image_object.error));
                    failed = true;
                    continue;
                }
                std::vector<toolchain::Object> objects{
                    toolchain::Object{input, image_object.object}};
                const auto linked = toolchain::link(objects, link_options);
                if (!linked.ok) {
                    diagnostics.push_back(
                        error_diagnostic("KR16 image: " + linked.error));
                    failed = true;
                    continue;
                }
                if (!write_binary_file(emit_path(".kr16"), linked.image, error)) {
                    diagnostics.push_back(error_diagnostic(error));
                    failed = true;
                }
            }
            continue;
        }

        if ((emit_object || emit_object_container) &&
            (target_id == krc::target::TargetId::Kr216 ||
             target_id == krc::target::TargetId::Kr232)) {
            const auto assembled =
                toolchain::assemble(compiled.assembly, compiled.debug_info, file_options);
            if (!assembled.ok) {
                diagnostics.push_back(error_diagnostic(assembled.error));
                failed = true;
                continue;
            }
            if (!write_binary_file(emit_path(".kro"), assembled.object, error)) {
                diagnostics.push_back(error_diagnostic(error));
                failed = true;
            }
            continue;
        }

        if (emit_object && target_id == krc::target::TargetId::Kr32) {
            const auto assembled =
                toolchain::assemble(compiled.assembly, compiled.debug_info, file_options);
            if (!assembled.ok) {
                diagnostics.push_back(error_diagnostic(assembled.error));
                failed = true;
                continue;
            }
            if (!write_binary_file(emit_path(".kro"), assembled.object, error)) {
                diagnostics.push_back(error_diagnostic(error));
                failed = true;
            }
        }
    }

    if (!diagnostics.empty()) {
        std::cerr << toolchain::format_diagnostics(diagnostics);
    }
    if (failed || toolchain::has_errors(diagnostics)) {
        return 1;
    }

    // `--dump-stats` counts as a request: D-049 makes it the metric every pass is
    // judged by, so asking for the numbers is a complete invocation rather than a
    // missing output.
    if (syntax_only || preprocess_only || dump_ast || dump_ir || dump_stats ||
        emit_assembly || emit_object) {
        return 0;
    }

    std::cerr << "krc: the " << profile.name
              << " front end parsed the input and built IR; code generation is not implemented yet\n";
    return 2;
}
