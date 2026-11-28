#include "krc/toolchain.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "asm/kr16_assembler.h"
#include "linker/kr232_image.h"
#include "asm/kr16_object.h"
#include "asm/kr216_encoder.h"
#include "asm/kr232_assembler.h"
#include "backend/kr232_backend.h"
#include "asm/kr232_object.h"
#include "asm/kr216_object.h"
#include "asm/kr32_assembler.h"
#include "asm/kro_writer.h"
#include "backend/kr16_backend.h"
#include "backend/kr216_backend.h"
#include "backend/kr32_backend.h"
#include "ir/ir.h"
#include "ir/passes.h"
#include "irgen/c_irgen.h"
#include "krc/frontend/ast.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"
#include "linker/kr16_image.h"
#include "linker/kr216_image.h"
#include "linker/archive.h"
#include "linker/kro_object.h"
#include "linker/linker.h"

namespace krc::toolchain {
namespace {

target::TargetId to_target_id(Target target) {
    switch (target) {
    case Target::Kr16:
        return target::TargetId::Kr16;
    case Target::Kr64:
        return target::TargetId::Kr64;
    case Target::Kr216:
        return target::TargetId::Kr216;
    case Target::Kr232:
        return target::TargetId::Kr232;
    case Target::Kr32:
        break;
    }
    return target::TargetId::Kr32;
}

std::string target_name(Target target) {
    switch (target) {
    case Target::Kr16:
        return "kr16";
    case Target::Kr64:
        return "kr64";
    case Target::Kr216:
        return "kr216";
    case Target::Kr232:
        return "kr232";
    case Target::Kr32:
        break;
    }
    return "kr32";
}

std::vector<Diagnostic> collect_diagnostics(frontend::DiagnosticEngine& diagnostics,
                                            const frontend::SourceManager& sources) {
    // Stable-sorted exactly as the CLI has always printed them: by file load
    // order, then offset, then severity.
    diagnostics.sort();
    std::vector<Diagnostic> converted;
    converted.reserve(diagnostics.diagnostics().size());
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        Diagnostic output;
        output.severity = diagnostic.severity;
        output.message = diagnostic.message;
        output.notes = diagnostic.notes;
        if (diagnostic.location.valid()) {
            const auto line_column = sources.line_column(diagnostic.location);
            output.file = std::string(sources.file_name(diagnostic.location));
            output.line = line_column.line;
            output.column = line_column.column;
            output.line_text = std::string(sources.line_text(diagnostic.location));
        }
        converted.push_back(std::move(output));
    }
    return converted;
}

void finish_compile(CompileResult& result, frontend::DiagnosticEngine& diagnostics,
                    const frontend::SourceManager& sources) {
    result.diagnostics = collect_diagnostics(diagnostics, sources);
    result.ok = !has_errors(result.diagnostics);
}

void finish_preprocess(PreprocessResult& result, frontend::DiagnosticEngine& diagnostics,
                       const frontend::SourceManager& sources) {
    result.diagnostics = collect_diagnostics(diagnostics, sources);
    result.ok = !has_errors(result.diagnostics);
}

frontend::Preprocessor::Options preprocessor_options(const Options& options) {
    frontend::Preprocessor::Options output;
    output.include_paths.reserve(options.include_paths.size());
    for (const auto& path : options.include_paths) {
        output.include_paths.emplace_back(path);
    }
    output.defines = options.defines;
    return output;
}

std::string format_stats(const ir::Module& module, const target::TargetProfile& profile,
                         backend::OptimizationLevel level) {
    std::ostringstream out;
    std::size_t total_instructions = 0;
    std::size_t total_blocks = 0;
    std::uint64_t total_frame_bytes = 0;
    for (const auto& function : module.functions()) {
        if (!function) {
            continue;
        }
        std::size_t instructions = 0;
        for (const auto& block : function->blocks) {
            if (block) {
                instructions += block->instructions.size();
            }
        }
        total_instructions += instructions;
        total_blocks += function->blocks.size();
        std::uint32_t frame = 0;
        if (profile.id == target::TargetId::Kr16) {
            frame = backend::Kr16Backend(module, profile, level).frame_bytes(*function);
        } else if (profile.id == target::TargetId::Kr216) {
            frame = backend::Kr216Backend(module, profile, level).frame_bytes(*function);
        } else {
            frame = backend::Kr32Backend(module, profile, level).frame_bytes(*function);
        }
        total_frame_bytes += frame;
        out << "stats\t" << function->name << "\t" << instructions << "\t"
            << function->blocks.size() << "\t" << frame << "\n";
    }
    out << "stats\tTOTAL\t" << total_instructions << "\t" << total_blocks << "\t"
        << total_frame_bytes << "\n";
    return out.str();
}

backend::OptimizationLevel optimization_level(const Options& options) {
    if (options.optimize >= 3) {
        return backend::OptimizationLevel::Os;
    }
    if (options.optimize == 2) {
        return backend::OptimizationLevel::O2;
    }
    return options.optimize > 0 ? backend::OptimizationLevel::O1
                                : backend::OptimizationLevel::O0;
}

void append_debug_info(linker::KroObject& object, std::string_view debug_info) {
    if (debug_info.empty()) {
        return;
    }
    linker::KroSection section;
    section.name = ".debug_info";
    section.type = linker::KroSectionType::Debug;
    section.flags = 0;
    section.alignment = 1;
    section.data.assign(debug_info.begin(), debug_info.end());
    section.size = section.data.size();
    object.sections.push_back(std::move(section));
}

namespace {

// `.target NAME` is a toolchain directive rather than an instruction: a generated
// assembly file carries it so a mismatched file is caught before it is assembled
// into a wrong object. A file without one assembles as before, because hand
// written sources predate the directive and stay valid.
std::optional<std::string> declared_target(std::string_view assembly) {
    std::size_t position = 0;
    while (position < assembly.size()) {
        const auto newline = assembly.find('\n', position);
        const auto end = newline == std::string_view::npos ? assembly.size() : newline;
        auto line = assembly.substr(position, end - position);
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.remove_prefix(1);
        }
        if (line.rfind(".target", 0) == 0) {
            auto rest = line.substr(std::string_view(".target").size());
            while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
                rest.remove_prefix(1);
            }
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) {
                rest.remove_suffix(1);
            }
            return std::string(rest);
        }
        position = end + 1;
    }
    return std::nullopt;
}

// Removes the directive so the target assembler never sees a line it would have
// to know about; one place validates it, none has to parse it.
void strip_target_directive(std::string& assembly) {
    std::string cleaned;
    std::size_t position = 0;
    while (position < assembly.size()) {
        const auto newline = assembly.find('\n', position);
        const auto end = newline == std::string::npos ? assembly.size() : newline;
        auto line = std::string_view(assembly).substr(position, end - position);
        auto probe = line;
        while (!probe.empty() && (probe.front() == ' ' || probe.front() == '\t')) {
            probe.remove_prefix(1);
        }
        if (probe.rfind(".target", 0) != 0) {
            cleaned.append(line);
            if (newline != std::string::npos) {
                cleaned.push_back('\n');
            }
        }
        position = end + 1;
    }
    assembly = std::move(cleaned);
}

}  // namespace
AssembleResult assemble_impl(std::string_view assembly, std::string_view debug_info,
                             const Options& options) {
    AssembleResult result;
    const auto id = to_target_id(options.target);
    const auto& profile = target::get_target(id);
    linker::KroObject object;
    std::string error;

    std::string text(assembly);
    if (const auto declared = declared_target(text)) {
        const auto expected = std::string(target::to_string(id));
        if (*declared != expected) {
            result.error = "this assembly declares .target " + *declared +
                           " but the assembler was asked for " + expected;
            return result;
        }
    }
    strip_target_directive(text);
    assembly = text;

    if (id == target::TargetId::Kr16) {
        asmkr16::AssemblyResult assembled;
        if (!asmkr16::assemble(assembly, assembled, error)) {
            result.error = "KR16 assembler: " + error;
            return result;
        }
        if (!asmkr16::make_object(assembled, profile, object, error)) {
            result.error = "KR16 object: " + error;
            return result;
        }
        append_debug_info(object, debug_info);
        result.listing = std::move(assembled.listing);
    } else if (id == target::TargetId::Kr216) {
        asmkr216::AssembledProgram assembled;
        if (!asmkr216::assemble_text(assembly, assembled, error)) {
            result.error = "KR216 assembler: " + error;
            return result;
        }
        if (!asmkr216::make_object(assembled, profile, object, error)) {
            result.error = "KR216 object: " + error;
            return result;
        }
        append_debug_info(object, debug_info);
        result.listing = std::move(assembled.listing);
    } else if (id == target::TargetId::Kr232) {
        asm_kr232::AssembledProgram assembled;
        const std::string_view recorded = options.source_name.empty()
                                             ? std::string_view("assembly")
                                             : std::string_view(options.source_name);
        if (!asm_kr232::assemble(assembly, recorded, assembled, error, options.far_calls)) {
            result.error = "KR232 assembler: " + error;
            return result;
        }
        if (!asm_kr232::make_object(assembled.program, profile, object, error)) {
            result.error = "KR232 object: " + error;
            return result;
        }
        // The kr232 assembler emits its own `.debug_info` bridge, so the option's
        // bridge is not appended here; two bridges would duplicate every record.
        result.listing = std::move(assembled.listing);
    } else if (id == target::TargetId::Kr32) {
        asmkr32::AssemblyResult assembled;
        if (!asmkr32::assemble(assembly, assembled, error)) {
            result.error = "KR32 assembler: " + error;
            return result;
        }
        if (!asmkr32::make_object(assembled, profile, object, error)) {
            result.error = "KRO writer: " + error;
            return result;
        }
        append_debug_info(object, debug_info);
        result.listing = std::move(assembled.listing);
    } else {
        result.error = "target '" + target_name(options.target) +
                       "' has no assembler";
        return result;
    }

    if (!linker::write_object(object, result.object, error)) {
        result.error = "KRO writer: " + error;
        return result;
    }
    result.ok = true;
    return result;
}

linker::LinkOptions link_options(const Options& options, std::uint64_t base,
                                 std::uint64_t default_base) {
    linker::LinkOptions output;
    output.base = base == 0 ? default_base : base;
    output.entry_symbol = options.entry.empty() ? "main" : options.entry;
    output.gc_sections = options.gc_sections;
    for (const auto& placement : options.section_addresses) {
        output.section_addresses.emplace(placement.name, placement.address);
    }
    return output;
}

void fill_link_result(LinkResult& result, const linker::LinkedImage& image) {
    result.entry = image.entry;
    result.base = image.base;
    result.sections.reserve(image.sections.size());
    for (const auto& section : image.sections) {
        // Only a section with the alloc bit occupies memory; a debug section
        // carries flags 8 and no alloc bit, exactly as the linker places them.
        const bool allocated = (section.flags & 0x0001u) != 0;
        result.sections.push_back({section.name, section.address, section.size,
                                   section.alignment,
                                   allocated ? section.data : std::vector<std::uint8_t>{}});
    }
    result.symbols.reserve(image.symbols.size());
    for (const auto& symbol : image.symbols) {
        result.symbols.push_back({symbol.name, symbol.value, symbol.size,
                                  symbol.binding != linker::KroSymbolBinding::Local});
    }
}

// The facade's `link` result plus the linked address of each input object's
// `.text`. `build` needs the latter to turn section-relative listing rows into
// final addresses; no other caller does, so it is not part of `LinkResult`.
struct LinkedOutput {
    LinkResult result;
    std::vector<std::uint64_t> object_text_addresses;
};

LinkedOutput link_impl(std::span<const Object> objects, const Options& options) {
    LinkedOutput output;
    auto& result = output.result;
    if (objects.empty()) {
        result.error = "linker requires at least one object";
        return output;
    }

    std::vector<linker::KroObject> parsed;
    parsed.reserve(objects.size());
    for (const auto& object : objects) {
        linker::KroObject read;
        std::string error;
        if (!linker::read_object(object.bytes, read, error)) {
            result.error = object.name.empty() ? error : object.name + ": " + error;
            return output;
        }
        parsed.push_back(std::move(read));
    }

    const auto target_id = parsed.front().target_id;
    const auto default_base = target_id == 2   ? std::uint64_t{0x100}
                              : target_id == 4 ? std::uint64_t{0x200}
                              : target_id == 5 ? std::uint64_t{0x200}
                                               : std::uint64_t{0x00010000};

    linker::LinkedImage image;
    std::string error;
    if (!linker::link_objects(parsed, link_options(options, options.base, default_base),
                              image, error)) {
        result.error = error;
        return output;
    }
    if (!linker::attach_kdbg(image, options.source_root, error)) {
        result.error = error;
        return output;
    }

    if (target_id == 2) {
        const linker::LinkedSection* text = nullptr;
        for (const auto& section : image.sections) {
            if (section.type == linker::KroSectionType::Text) {
                text = &section;
                break;
            }
        }
        if (text == nullptr) {
            result.error = "KR16 image has no text section";
            return output;
        }
        if (image.entry > 0xFFFF || image.base > 0xFFFF) {
            result.error = "KR16 address exceeds 16 bits";
            return output;
        }
        std::vector<linker::LinkedSection> extra;
        for (const auto& section : image.sections) {
            if (section.name == ".text") {
                continue;
            }
            // `.debug_info` is the bridge `attach_kdbg` consumed, not a shipped
            // section. The image carries the converted `.kdbg` and every
            // allocated section; other debug data stays in the object files.
            if (section.type == linker::KroSectionType::Debug &&
                section.name != ".kdbg") {
                continue;
            }
            extra.push_back(section);
        }
        if (!linker::write_kr16_image(text->data,
                                      static_cast<std::uint16_t>(image.base),
                                      static_cast<std::uint16_t>(image.entry), extra,
                                      result.image, error)) {
            result.error = error;
            return output;
        }
    } else if (target_id == 4) {
        linker::LinkedImage filtered = image;
        filtered.sections.clear();
        for (const auto& section : image.sections) {
            // `.debug_info` is the bridge `attach_kdbg` consumed; the image
            // carries the converted `.kdbg` and every allocated section.
            if (section.type == linker::KroSectionType::Debug &&
                section.name != ".kdbg") {
                continue;
            }
            filtered.sections.push_back(section);
        }
        if (image.base > 0xFFFF || image.entry > 0xFFFF) {
            result.error = "KR216 address exceeds 16 bits";
            return output;
        }
        if (!linker::write_kr216_image(filtered, result.image, error)) {
            result.error = error;
            return output;
        }
    } else if (target_id == 5) {
        linker::LinkedImage filtered = image;
        filtered.sections.clear();
        for (const auto& section : image.sections) {
            // `.debug_info` is the bridge `attach_kdbg` consumed; the container
            // carries the converted `.kdbg` and every other section.
            if (section.type == linker::KroSectionType::Debug && section.name != ".kdbg") {
                continue;
            }
            filtered.sections.push_back(section);
        }
        if (!linker::write_kr232_container(filtered, result.image, error)) {
            result.error = error;
            return output;
        }
    } else if (!linker::write_kr32_executable(image, result.image, error)) {
        result.error = error;
        return output;
    }

    fill_link_result(result, image);
    output.object_text_addresses = image.object_text_addresses;

    // The kr32 OS reads its own symbols from a sibling `<image>.sym` container:
    // every section of the image plus `__sym_crc`, whose four bytes are the
    // CRC32 of the flat image the loader places in memory (symbols.h consumes
    // it and rejects a sidecar that does not match). The CRC is the same
    // polynomial and finalisation as the container writer's.
    if (options.symbol_sidecar && options.target == Target::Kr32) {
        const auto flat = flatten(result);
        const auto checksum = linker::crc32(flat);
        linker::LinkedImage augmented = image;
        linker::LinkedSection crc_section;
        crc_section.name = "__sym_crc";
        crc_section.type = linker::KroSectionType::Data;
        crc_section.flags = 0;
        crc_section.alignment = 1;
        crc_section.data = {static_cast<std::uint8_t>(checksum & 0xFFu),
                            static_cast<std::uint8_t>((checksum >> 8) & 0xFFu),
                            static_cast<std::uint8_t>((checksum >> 16) & 0xFFu),
                            static_cast<std::uint8_t>((checksum >> 24) & 0xFFu)};
        crc_section.size = crc_section.data.size();
        augmented.sections.push_back(crc_section);
        std::string sidecar_error;
        if (!linker::write_kr32_executable(augmented, result.sidecar, sidecar_error)) {
            result.error = sidecar_error;
            return output;
        }
    }
    result.ok = true;
    return output;
}

std::uint64_t linked_section_address(const std::vector<SectionInfo>& sections,
                                     std::string_view name) {
    for (const auto& section : sections) {
        if (section.name == name) {
            return section.address;
        }
    }
    return 0;
}

}  // namespace

std::string format_diagnostics(const std::vector<Diagnostic>& diagnostics) {
    std::string output;
    for (const auto& diagnostic : diagnostics) {
        if (!diagnostic.file.empty()) {
            output += diagnostic.file;
            output.push_back(':');
            output += std::to_string(diagnostic.line);
            output.push_back(':');
            output += std::to_string(diagnostic.column);
            output += ": ";
        }
        output += frontend::severity_name(diagnostic.severity);
        output += ": ";
        output += diagnostic.message;
        output.push_back('\n');

        if (!diagnostic.file.empty() && !diagnostic.line_text.empty() &&
            diagnostic.column > 0) {
            output += "  ";
            output += diagnostic.line_text;
            output.push_back('\n');
            output += "  ";
            const auto prefix_length = std::min<std::size_t>(
                diagnostic.column - 1, diagnostic.line_text.size());
            for (std::size_t index = 0; index < prefix_length; ++index) {
                output.push_back(diagnostic.line_text[index] == '\t' ? '\t' : ' ');
            }
            output += "^\n";
        }
        for (const auto& note : diagnostic.notes) {
            output += "  note: ";
            output += note;
            output.push_back('\n');
        }
    }
    return output;
}

bool has_errors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& diagnostic) {
                           return diagnostic.severity == frontend::Severity::Error ||
                                  diagnostic.severity == frontend::Severity::Fatal;
                       });
}

PreprocessResult preprocess(const Source& source, const Options& options) {
    PreprocessResult result;
    const auto& profile = target::get_target(to_target_id(options.target));
    frontend::SourceManager sources;
    frontend::DiagnosticEngine diagnostics(sources);
    frontend::Preprocessor preprocessor(sources, diagnostics, profile,
                                        preprocessor_options(options));
    const auto file = sources.add_buffer(source.name, std::string(source.text));
    result.tokens = preprocessor.preprocess(file);
    finish_preprocess(result, diagnostics, sources);
    return result;
}

CompileResult compile(const Source& source, const Options& options) {
    CompileResult result;
    if (source.name.empty()) {
        result.error = "source has no name";
        return result;
    }
    const auto id = to_target_id(options.target);
    const auto& profile = target::get_target(id);
    const auto level = optimization_level(options);

    // A target without a backend cannot generate code, whatever the source says.
    // Reporting that before the front end runs is what makes the message stable:
    // otherwise a source error pre-empts it and the caller learns about the wrong
    // problem. `-fsyntax-only` and `-E` still work for such a target.
    const bool needs_backend = options.stage == Stage::Assembly;
    if (!profile.backend_available && needs_backend) {
        frontend::SourceManager sources;
        frontend::DiagnosticEngine diagnostics(sources);
        diagnostics.report(frontend::Severity::Error,
                           "target '" + target_name(options.target) +
                               "' has no code generator yet");
        finish_compile(result, diagnostics, sources);
        return result;
    }

    // A target whose profile has no C++ refuses C++ mode rather than parsing a
    // language its ABI never described (documentation audit B2).
    if (options.cxx && !profile.has_cpp) {
        frontend::SourceManager sources;
        frontend::DiagnosticEngine diagnostics(sources);
        diagnostics.report(frontend::Severity::Error,
                           "target '" + target_name(options.target) +
                               "' does not support C++");
        finish_compile(result, diagnostics, sources);
        return result;
    }

    frontend::SourceManager sources;
    frontend::DiagnosticEngine diagnostics(sources);
    frontend::Preprocessor preprocessor(sources, diagnostics, profile,
                                        preprocessor_options(options));
    const auto file = sources.add_buffer(source.name, std::string(source.text));
    auto tokens = preprocessor.preprocess(file);

    frontend::Parser parser(std::move(tokens), diagnostics, sources, options.cxx);
    auto ast = parser.parse_translation_unit();
    if (options.dump_ast && ast) {
        std::ostringstream out;
        frontend::dump_ast(*ast, out);
        result.ast_dump = out.str();
    }
    if (!ast || parser.had_error()) {
        finish_compile(result, diagnostics, sources);
        return result;
    }

    frontend::Sema sema(diagnostics, profile, options.cxx);
    sema.analyze(*ast);
    if (diagnostics.has_errors() || options.stage == Stage::Syntax) {
        finish_compile(result, diagnostics, sources);
        return result;
    }

    irgen::CIRGen irgen(sema, *ast, profile, diagnostics);
    auto module = irgen.generate();
    if (!module) {
        finish_compile(result, diagnostics, sources);
        return result;
    }
    // Lowering, not optimisation: an identity cast computes nothing, and D-049
    // makes selection quality a property of the lowering, so this runs at -O0
    // too.
    static_cast<void>(ir::fold_identity_casts(*module));

    ir::Verifier verifier;
    std::string verify_error;
    if (!verifier.verify(*module, verify_error)) {
        diagnostics.report(frontend::Severity::Error, "IR verifier: " + verify_error);
    }
    if (options.optimize > 0 && !diagnostics.has_errors()) {
        ir::PassChange change;
        const auto folded = ir::constant_folding(*module);
        change.instructions_removed += folded.instructions_removed;
        change.instructions_folded += folded.instructions_folded;
        const auto forwarded = ir::forward_alloca_loads(*module);
        change.instructions_removed += forwarded.instructions_removed;
        const auto dead_allocas = ir::eliminate_dead_alloca_stores(*module);
        change.instructions_removed += dead_allocas.instructions_removed;
        // E6 before every other promotion pass: it creates the phis, so the
        // edges they write on are split first and the passes below then see SSA
        // values where a slot round trip used to be. Only the KR32 and KR16
        // backends lower phi nodes; KR216 and KR232 keep the alloca round trip
        // until theirs do, and every pass below is independent of it.
        if (id == target::TargetId::Kr32 || id == target::TargetId::Kr16) {
            const auto edges = ir::split_critical_edges(*module);
            change.edges_split += edges.edges_split;
            const auto across =
                ir::promote_cross_block_allocas(*module);
            change.instructions_removed += across.instructions_removed;
        }
        // Promotion runs before CSE deliberately: E2 shares only expressions
        // whose operands are identical SSA values, and until the alloca round
        // trips are gone every variable read is a distinct load.
        //
        // A named source variable is a memory object named `<variable>.addr`, and
        // promoting it deletes the slot its debug record describes. KDBG can only
        // describe a frame home without a `.kdbg_loc` producer, so ADR 0052's rule
        // applies to every target that has none: do not promote a recorded alloca
        // until a reader honours register locations. kr232 and kr216 therefore
        // pass `false` at every level, including `-O2`
        // (docs/register-promoted-locals.md, Track A item 5;
        // docs/symbols.md, variable policy). kr16 and kr32 keep the historical
        // level-only gate; their `-O2` loss is the same open gap, tracked in
        // docs/register-promoted-locals.md §1.2.
        const bool target_describes_promoted_locals =
            id != target::TargetId::Kr232 && id != target::TargetId::Kr216;
        const auto promoted = ir::alloca_promotion(
            *module, options.optimize >= 2 && target_describes_promoted_locals);
        change.instructions_removed += promoted.instructions_removed;
        if (options.optimize >= 2) {
            const auto inlined = ir::inline_small_functions(*module);
            change.functions_inlined += inlined.functions_inlined;
        }
        const auto shared = ir::copy_propagation(*module);
        change.instructions_removed += shared.instructions_removed;
        change.instructions_folded += shared.instructions_folded;
        // Dead code before the block pass: removing an instruction can empty a
        // block, and the block pass then has one more to collapse.
        const auto unused = ir::dead_code_elimination(*module);
        change.instructions_removed += unused.instructions_removed;
        // Last, because it removes blocks: the value passes above are written
        // against the CFG they were given.
        const auto simplified = ir::empty_block_elimination(*module);
        change.instructions_removed += simplified.instructions_removed;
        if (change.changed()) {
            std::string optimized_error;
            if (!verifier.verify(*module, optimized_error)) {
                diagnostics.report(frontend::Severity::Error,
                                   "IR verifier after optimiser: " + optimized_error);
            }
        }
    }

    if (options.dump_ir) {
        std::ostringstream out;
        ir::print_module(*module, out);
        result.ir_dump = out.str();
    }
    if (options.dump_stats) {
        result.stats = format_stats(*module, profile, level);
    }
    if (options.stage == Stage::Ir || diagnostics.has_errors()) {
        finish_compile(result, diagnostics, sources);
        return result;
    }

    if (!profile.backend_available) {
        diagnostics.report(frontend::Severity::Error,
                           "target '" + target_name(options.target) +
                               "' has no code generator yet");
        finish_compile(result, diagnostics, sources);
        return result;
    }

    std::string backend_error;
    if (id == target::TargetId::Kr232) {
        backend::Kr232Backend backend(*module, profile, level);
        auto assembly = backend.emit(backend_error);
        if (!assembly) {
            diagnostics.report(frontend::Severity::Error, "KR232 backend: " + backend_error);
        } else {
            result.assembly = std::move(*assembly);
            if (options.debug) {
                result.debug_info = backend.debug_info();
            }
        }
    } else if (id == target::TargetId::Kr16) {
        backend::Kr16Backend backend(*module, profile, level);
        auto assembly = backend.emit(backend_error);
        if (!assembly) {
            diagnostics.report(frontend::Severity::Error,
                               "KR16 backend: " + backend_error);
        } else {
            result.assembly = std::move(*assembly);
            if (options.debug) {
                result.debug_info = backend.debug_info();
            }
        }
    } else if (id == target::TargetId::Kr216) {
        backend::Kr216Backend backend(*module, profile, level, options.source_name);
        auto assembly = backend.emit(backend_error);
        if (!assembly) {
            diagnostics.report(frontend::Severity::Error,
                               "KR216 backend: " + backend_error);
        } else {
            result.assembly = std::move(*assembly);
            if (options.debug) {
                result.debug_info = backend.debug_info();
            }
        }
    } else {
        backend::Kr32Backend backend(*module, profile, level);
        auto assembly = backend.emit(backend_error);
        if (!assembly) {
            diagnostics.report(frontend::Severity::Error,
                               "KR32 backend: " + backend_error);
        } else {
            result.assembly = std::move(*assembly);
            if (options.debug) {
                result.debug_info = backend.debug_info();
            }
        }
    }
    finish_compile(result, diagnostics, sources);
    // Every backend emits the directive, so a saved `-S` file says which machine
    // it is for even after it is renamed.
    if (!result.assembly.empty() && result.error.empty()) {
        const auto declared_id = to_target_id(options.target);
        result.assembly.insert(0, ".target " + std::string(target::to_string(declared_id)) + "\n");
    }
    return result;
}

AssembleResult assemble(std::string_view assembly, const Options& options) {
    return assemble_impl(assembly, {}, options);
}

AssembleResult assemble(std::string_view assembly, std::string_view debug_info,
                        const Options& options) {
    return assemble_impl(assembly, debug_info, options);
}

bool is_relocatable_object(std::span<const std::uint8_t> bytes) {
    return linker::is_object(bytes);
}

bool is_linker_archive(std::span<const std::uint8_t> bytes) {
    return linker::is_archive(bytes);
}
std::string map_text(const LinkResult& linked) {
    std::ostringstream out;
    out << "base=0x" << std::hex << linked.base << " entry=0x" << linked.entry << std::dec
        << '\n';
    out << "section address size\n";
    for (const auto& section : linked.sections) {
        out << section.name << " 0x" << std::hex << section.address << std::dec << ' '
            << section.size << '\n';
    }
    out << "symbol value size binding\n";
    for (const auto& symbol : linked.symbols) {
        out << symbol.name << " 0x" << std::hex << symbol.value << std::dec << ' '
            << symbol.size << ' ' << (symbol.global ? "global" : "local") << '\n';
    }
    return out.str();
}
LinkResult link(std::span<const Object> objects, const Options& options) {
    return link_impl(objects, options).result;
}

LinkResult link_with_listing(std::span<const Object> objects,
                             std::span<const std::vector<ListingRow>> listings,
                             const Options& options) {
    auto linked = link_impl(objects, options);
    auto result = std::move(linked.result);
    if (!result.ok) {
        return result;
    }
    // A `.text` row is an offset in its object's text, so it moves by that
    // object's placed address; any other section's rows move by the merged
    // section's address, which is what the linker can promise for a section it
    // merges across objects.
    for (std::size_t index = 0; index < listings.size(); ++index) {
        const auto object_base = index < linked.object_text_addresses.size()
                                     ? linked.object_text_addresses[index]
                                     : 0;
        for (auto row : listings[index]) {
            const auto address = linked_section_address(result.sections, row.section);
            row.address = row.section == ".text" ? object_base + row.address
                                                 : address + row.address;
            result.listing.push_back(std::move(row));
        }
    }
    return result;
}

std::vector<std::uint8_t> flatten(const LinkResult& linked) {
    std::uint64_t end = linked.base;
    for (const auto& section : linked.sections) {
        if (section.bytes.empty()) {
            continue;
        }
        end = std::max(end, section.address + section.size);
    }
    std::vector<std::uint8_t> image(static_cast<std::size_t>(end - linked.base), 0);
    for (const auto& section : linked.sections) {
        if (section.bytes.empty()) {
            continue;
        }
        const auto offset = static_cast<std::size_t>(section.address - linked.base);
        std::copy(section.bytes.begin(), section.bytes.begin() +
                                            static_cast<std::ptrdiff_t>(section.size),
                  image.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return image;
}

BuildResult build(std::span<const Source> sources, const Options& options) {
    BuildResult result;
    if (sources.empty()) {
        result.error = "build requires at least one source";
        return result;
    }
    std::vector<Object> objects;
    std::vector<AssembleResult> assembled;
    objects.reserve(sources.size());
    assembled.reserve(sources.size());
    for (const auto& source : sources) {
        auto compiled = compile(source, options);
        result.diagnostics.insert(result.diagnostics.end(),
                                  compiled.diagnostics.begin(),
                                  compiled.diagnostics.end());
        if (!compiled.ok) {
            result.error = compiled.error.empty() ? "compilation failed" : compiled.error;
            return result;
        }
        auto object = assemble(compiled.assembly, compiled.debug_info, options);
        result.diagnostics.insert(result.diagnostics.end(),
                                  object.diagnostics.begin(), object.diagnostics.end());
        if (!object.ok) {
            result.error = source.name + ": " + object.error;
            return result;
        }
        objects.push_back(Object{source.name, object.object});
        assembled.push_back(std::move(object));
    }

    auto linked = link_impl(objects, options);
    if (!linked.result.ok) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  linked.result.diagnostics.begin(),
                                  linked.result.diagnostics.end());
        result.error = linked.result.error;
        return result;
    }
    result.image = std::move(linked.result.image);
    result.sections = std::move(linked.result.sections);
    result.symbols = std::move(linked.result.symbols);
    result.entry = linked.result.entry;
    result.base = linked.result.base;

    for (std::size_t index = 0; index < assembled.size(); ++index) {
        const auto object_base = index < linked.object_text_addresses.size()
                                     ? linked.object_text_addresses[index]
                                     : 0;
        for (auto row : assembled[index].listing) {
            const auto address = linked_section_address(result.sections, row.section);
            row.address = row.section == ".text" ? object_base + row.address
                                                 : address + row.address;
            result.listing.push_back(std::move(row));
        }
    }
    result.ok = true;
    return result;
}

}  // namespace krc::toolchain
