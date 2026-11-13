#include <filesystem>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asm/kr16_assembler.h"
#include "asm/kr16_object.h"
#include "asm/kr32_assembler.h"
#include "asm/kro_writer.h"
#include "backend/kr32_backend.h"
#include "backend/kr16_backend.h"
#include "debug/kdbg.h"
#include "ir/ir.h"
#include "irgen/c_irgen.h"
#include "krc/frontend/ast.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"
#include "krc/version.h"
#include "linker/kro_object.h"
#include "linker/kr16_image.h"

#ifndef KRC_SYSROOT_DIR
#define KRC_SYSROOT_DIR "sysroot"
#endif

namespace {

void print_usage(std::ostream& out) {
    out << "krc " << krc::kVersionString << " — KRCompiler driver\n"
        << "\n"
        << "Usage: krc [options] files...\n"
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
        << "  --dump-symbols FILE    Dump KDBG symbols from a .k32 image\n"
        << "  --json                 Use JSON with --dump-symbols\n"
        << "  -S                     Emit KR32 assembly to stdout, or to -o FILE\n"
        << "  -c                     Emit a relocatable KRO object\n"
        << "  --emit-object          Write a KR16 KRO object instead of .kex\n"
        << "  -o FILE                Output path for -S or -c\n"
        << "  -I DIR                 Add an include search directory\n"
        << "  --sysroot DIR          Use DIR as the target sysroot\n"
        << "\n"
        << "KR32 code generation is complete for the implemented C and C++ subset;\n"
        << "KR16 compiles C to a .kex image or a KRO object, and KR64 has no backend.\n";
}

int fail(std::string_view message) {
    std::cerr << "krc: error: " << message << '\n';
    return 2;
}

void add_include_directory(std::vector<std::filesystem::path>& paths,
                           const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::exists(path, error) && !error) {
        paths.push_back(path);
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string target_name = "kr32";
    std::vector<std::string> inputs;
    std::vector<std::filesystem::path> include_paths;
    bool syntax_only = false;
    bool preprocess_only = false;
    bool dump_ast = false;
    bool dump_ir = false;
    bool emit_assembly = false;
    bool emit_object = false;
    bool emit_object_container = false;
    bool dump_symbols = false;
    bool json_output = false;
    bool cxx_mode = false;
    std::string output_path;
    std::string sysroot_path = KRC_SYSROOT_DIR;

    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];

        if (argument == "--version") {
            std::cout << "krc " << krc::kVersionString << '\n';
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
        if (argument == "--dump-symbols") {
            if (i + 1 >= argc) {
                return fail("--dump-symbols requires a file");
            }
            dump_symbols = true;
            inputs.emplace_back(argv[++i]);
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

    if (inputs.empty()) {
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
        if (inputs.size() != 1) {
            return fail("--dump-symbols accepts exactly one image");
        }
        std::ifstream file(inputs.front(), std::ios::binary);
        if (!file) {
            return fail("cannot open '" + inputs.front() + "'");
        }
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                              std::istreambuf_iterator<char>());
        std::span<const std::uint8_t> section;
        std::string debug_error;
        if (!krc::debug::find_k32_section(bytes, ".kdbg", section, debug_error)) {
            return fail(debug_error);
        }
        krc::debug::KdbgImage image;
        if (!krc::debug::decode_kdbg(section, image, debug_error)) {
            return fail(debug_error);
        }
        std::cout << (json_output ? krc::debug::dump_kdbg_json(image)
                                  : krc::debug::dump_kdbg_text(image));
        return 0;
    }

    const std::filesystem::path sysroot(sysroot_path);
    add_include_directory(include_paths, sysroot / "include");
    add_include_directory(include_paths, sysroot / std::string(profile.name) / "include");

    krc::frontend::SourceManager sources;
    krc::frontend::DiagnosticEngine diagnostics(sources);
    krc::frontend::Preprocessor::Options options;
    options.include_paths = include_paths;

    for (const auto& input : inputs) {
        const auto input_extension =
            std::filesystem::path(input).extension().string();
        if (emit_object_container && input_extension == ".s") {
            std::ifstream source_file(input, std::ios::binary);
            if (!source_file) {
                diagnostics.report(krc::frontend::Severity::Error,
                                   "cannot open '" + input + "'");
                continue;
            }
            const std::string source_text(
                (std::istreambuf_iterator<char>(source_file)),
                std::istreambuf_iterator<char>());
            krc::asmkr16::AssemblyResult assembled;
            std::string assemble_error;
            if (!krc::asmkr16::assemble(source_text, assembled,
                                        assemble_error)) {
                diagnostics.report(krc::frontend::Severity::Error,
                                   "KR16 assembler: " + assemble_error);
                continue;
            }
            krc::linker::KroObject object;
            std::string object_error;
            if (!krc::asmkr16::make_object(assembled, profile, object,
                                           object_error)) {
                diagnostics.report(krc::frontend::Severity::Error,
                                   "KR16 object: " + object_error);
                continue;
            }
            std::vector<std::uint8_t> bytes;
            if (!krc::linker::write_object(object, bytes, object_error)) {
                diagnostics.report(krc::frontend::Severity::Error,
                                   "KR16 object: " + object_error);
                continue;
            }
            auto path = output_path.empty()
                            ? std::filesystem::path(input).replace_extension(
                                  ".kro")
                            : std::filesystem::path(output_path);
            std::ofstream file(path, std::ios::binary);
            if (!file) {
                diagnostics.report(krc::frontend::Severity::Error,
                                   "cannot open object output '" +
                                       path.string() + "'");
                continue;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(bytes.size()));
            continue;
        }
        krc::frontend::Preprocessor preprocessor(sources, diagnostics, profile, options);
        auto tokens = preprocessor.preprocess_file(input);
        if (!tokens) {
            continue;
        }
        if (preprocess_only) {
            bool first = true;
            for (const auto& token : *tokens) {
                if (!first) {
                    std::cout << ' ';
                }
                std::cout << token.text;
                first = false;
            }
            std::cout << '\n';
            continue;
        }

        krc::frontend::Parser parser(std::move(*tokens), diagnostics, sources, cxx_mode);
        auto ast = parser.parse_translation_unit();
        if (dump_ast && ast) {
            krc::frontend::dump_ast(*ast, std::cout);
        }
        if (ast && !parser.had_error()) {
            krc::frontend::Sema sema(diagnostics, profile, cxx_mode);
            sema.analyze(*ast);
            if ((dump_ir || emit_assembly || emit_object) && !diagnostics.has_errors()) {
                krc::irgen::CIRGen irgen(sema, *ast, profile, diagnostics);
                auto module = irgen.generate();
                if (module) {
                    std::string verify_error;
                    krc::ir::Verifier verifier;
                    if (!verifier.verify(*module, verify_error)) {
                        diagnostics.report(krc::frontend::Severity::Error,
                                           "IR verifier: " + verify_error);
                    }
                    if (dump_ir) {
                        krc::ir::print_module(*module, std::cout);
                    }
                    if ((emit_assembly || emit_object) &&
                        profile.id == krc::target::TargetId::Kr16) {
                        krc::backend::Kr16Backend backend(*module, profile);
                        std::string backend_error;
                        auto assembly = backend.emit(backend_error);
                        if (!assembly) {
                            diagnostics.report(krc::frontend::Severity::Error,
                                               "KR16 backend: " +
                                                   backend_error);
                        } else if (emit_object) {
                            std::string source;
                            if (!emit_object_container) {
                                source =
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
                            }
                            source += *assembly;
                            krc::asmkr16::AssemblyResult assembled;
                            std::string assemble_error;
                            if (!krc::asmkr16::assemble(source, assembled,
                                                        assemble_error)) {
                                diagnostics.report(
                                    krc::frontend::Severity::Error,
                                    "KR16 assembler: " + assemble_error);
                            } else if (emit_object_container) {
                                krc::linker::KroObject object;
                                std::string object_error;
                                if (!krc::asmkr16::make_object(
                                        assembled, profile, object,
                                        object_error)) {
                                    diagnostics.report(
                                        krc::frontend::Severity::Error,
                                        "KR16 object: " + object_error);
                                } else {
                                    std::vector<std::uint8_t> bytes;
                                    if (!krc::linker::write_object(
                                            object, bytes, object_error)) {
                                        diagnostics.report(
                                            krc::frontend::Severity::Error,
                                            "KR16 object: " + object_error);
                                    } else {
                                        auto path =
                                            output_path.empty()
                                                ? std::filesystem::path(input)
                                                      .replace_extension(".kro")
                                                : std::filesystem::path(
                                                      output_path);
                                        std::ofstream file(path,
                                                           std::ios::binary);
                                        if (!file) {
                                            diagnostics.report(
                                                krc::frontend::Severity::Error,
                                                "cannot open object output '" +
                                                    path.string() + "'");
                                        } else {
                                            file.write(
                                                reinterpret_cast<const char*>(
                                                    bytes.data()),
                                                static_cast<std::streamsize>(
                                                    bytes.size()));
                                        }
                                    }
                                }
                            } else {
                                bool has_main = false;
                                for (const auto& symbol : assembled.symbols) {
                                    has_main = has_main || symbol.name == "main";
                                }
                                if (!has_main) {
                                    diagnostics.report(
                                        krc::frontend::Severity::Error,
                                        "KR16 image has no 'main' entry");
                                } else {
                                    std::vector<std::uint8_t> bytes;
                                    std::string image_error;
                                    if (!krc::linker::write_kr16_image(
                                            assembled.text, 0x0100, bytes,
                                            image_error)) {
                                        diagnostics.report(
                                            krc::frontend::Severity::Error,
                                            "KR16 image: " + image_error);
                                    } else {
                                        auto path =
                                            output_path.empty()
                                                ? std::filesystem::path(input)
                                                      .replace_extension(".kex")
                                                : std::filesystem::path(
                                                      output_path);
                                        std::ofstream file(path,
                                                           std::ios::binary);
                                        if (!file) {
                                            diagnostics.report(
                                                krc::frontend::Severity::Error,
                                                "cannot open image output '" +
                                                    path.string() + "'");
                                        } else {
                                            file.write(
                                                reinterpret_cast<const char*>(
                                                    bytes.data()),
                                                static_cast<std::streamsize>(
                                                    bytes.size()));
                                        }
                                    }
                                }
                            }
                        } else {
                            if (emit_assembly) {
                                if (output_path.empty()) {
                                    std::cout << *assembly;
                                } else {
                                    std::ofstream file(output_path, std::ios::binary);
                                    if (!file) {
                                        diagnostics.report(
                                            krc::frontend::Severity::Error,
                                            "cannot open assembly output '" + output_path + "'");
                                    } else {
                                        file << *assembly;
                                    }
                                }
                            }
                        }
                    } else if ((emit_assembly || emit_object) &&
                               !profile.backend_available) {
                        diagnostics.report(krc::frontend::Severity::Error,
                                           "target '" + target_name +
                                               "' has no code generator yet");
                    } else if ((emit_assembly || emit_object) &&
                               profile.id == krc::target::TargetId::Kr32) {
                        krc::backend::Kr32Backend backend(*module, profile);
                        std::string backend_error;
                        auto assembly = backend.emit(backend_error);
                        if (!assembly) {
                            diagnostics.report(krc::frontend::Severity::Error,
                                               "KR32 backend: " + backend_error);
                        } else {
                            if (emit_assembly) {
                                if (output_path.empty()) {
                                    std::cout << *assembly;
                                } else {
                                    std::ofstream file(output_path, std::ios::binary);
                                    if (!file) {
                                        diagnostics.report(
                                            krc::frontend::Severity::Error,
                                            "cannot open assembly output '" + output_path + "'");
                                    } else {
                                        file << *assembly;
                                    }
                                }
                            }
                            // An object built from a failed code generation must
                            // not reach disk: a build that ignores the exit
                            // status would otherwise link and run it.
                            if (emit_object && !diagnostics.has_errors()) {
                                krc::asmkr32::AssemblyResult assembled;
                                std::string assemble_error;
                                if (!krc::asmkr32::assemble(*assembly, assembled,
                                                            assemble_error)) {
                                    diagnostics.report(krc::frontend::Severity::Error,
                                                       "KR32 assembler: " + assemble_error);
                                } else {
                                    krc::linker::KroObject object;
                                    std::string object_error;
                                    if (!krc::asmkr32::make_object(assembled, profile, object,
                                                                   object_error)) {
                                        diagnostics.report(krc::frontend::Severity::Error,
                                                           "KRO writer: " + object_error);
                                    } else {
                                        if (!backend.debug_info().empty()) {
                                            krc::linker::KroSection debug;
                                            debug.name = ".debug_info";
                                            debug.type = krc::linker::KroSectionType::Debug;
                                            debug.flags = 0;
                                            debug.alignment = 1;
                                            debug.data.assign(backend.debug_info().begin(),
                                                              backend.debug_info().end());
                                            debug.size = debug.data.size();
                                            object.sections.push_back(std::move(debug));
                                        }
                                        std::vector<std::uint8_t> bytes;
                                        std::string write_error;
                                        if (!krc::linker::write_object(object, bytes, write_error)) {
                                            diagnostics.report(krc::frontend::Severity::Error,
                                                               "KRO writer: " + write_error);
                                        } else {
                                            auto path = output_path.empty()
                                                            ? std::filesystem::path(input)
                                                                  .replace_extension(".kro")
                                                            : std::filesystem::path(output_path);
                                            std::ofstream file(path, std::ios::binary);
                                            if (!file) {
                                                diagnostics.report(
                                                    krc::frontend::Severity::Error,
                                                    "cannot open object output '" +
                                                        path.string() + "'");
                                            } else {
                                                file.write(reinterpret_cast<const char*>(
                                                               bytes.data()),
                                                           static_cast<std::streamsize>(
                                                               bytes.size()));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (emit_assembly || emit_object) {
                        diagnostics.report(krc::frontend::Severity::Error,
                                           "assembly/object emission is only implemented for kr32");
                    }
                }
            }
        }
    }

    if (diagnostics.has_errors() || diagnostics.warning_count() > 0) {
        diagnostics.sort();
        diagnostics.print(std::cerr);
    }
    if (diagnostics.has_errors()) {
        return 1;
    }

    if (syntax_only || preprocess_only || dump_ast || dump_ir || emit_assembly || emit_object) {
        return 0;
    }

    std::cerr << "krc: the " << profile.name
              << " front end parsed the input and built IR; code generation is not implemented yet\n";
    return 2;
}
