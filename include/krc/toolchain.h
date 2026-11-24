#pragma once

// Public build facade for the KR compiler.
//
// A host supplies source text and receives assembly, object bytes, image bytes,
// structured diagnostics and listing rows. The facade never writes or removes a
// file, never opens the source path or an output path, and never starts a
// subprocess: the host reads its own inputs and writes its own outputs. The one
// read it performs is a header a `#include` names, resolved against the include
// paths and the including source's own directory; a source with no `#include`
// needs no file at all.
//
// Every function is re-entrant. All compiler state -- source ids, type
// canonicalisation, the backend's debug buffer -- is per call, so two calls in
// one process cannot observe each other. An identical request produces
// identical bytes regardless of working directory, time or environment.
//
// Inputs are borrowed for the duration of the call. Outputs are owned by the
// caller. A user error returns a result with `ok == false` and at least one
// diagnostic or a non-empty `error`; a violated internal invariant still
// throws, and no exception is converted into an empty error message.

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "krc/frontend/diagnostics.h"
#include "krc/frontend/lexer.h"

namespace krc::toolchain {

// The VM targets the compiler knows. KR64 has a target descriptor but no
// backend, so it is accepted for front-end-only stages and rejected once a
// stage needs code generation or an assembler.
enum class Target : std::uint8_t {
    Kr32,
    Kr16,
    Kr64,
    Kr216,
    Kr232,
};

// How far `compile` runs. `-fsyntax-only` must not reach the backend, because a
// program the front end accepts can still be rejected during code generation,
// and a host that only asked for a syntax check must not see that error.
enum class Stage : std::uint8_t {
    Syntax,      // preprocess, parse, semantic analysis
    Ir,          // also lower to typed SSA IR (and verify it)
    Assembly,    // also emit target assembly
};

// One translation unit. `text` is borrowed for the call; `name` is both the
// display name in diagnostics and the name a quoted #include resolves against,
// exactly as a compiler would treat a file path.
struct Source {
    std::string name;
    std::string_view text;
};

// A relocatable object as bytes. `name` is diagnostic metadata only; the bytes
// are what `link` consumes, so an object read from disk and one produced by
// `assemble` are interchangeable.
struct Object {
    std::string name;
    std::vector<std::uint8_t> bytes;
};

// A section placement for `link`, equivalent to one `NAME ADDRESS` line in
// `krld`'s layout file.
struct SectionPlacement {
    std::string name;
    std::uint64_t address = 0;
};

struct Options {
    Target target = Target::Kr32;
    // C++ mode. Named explicitly rather than inferred from the source name: a
    // host may compile text that came from a string and has no extension.
    bool cxx = false;
    // Emit the textual `.debug_info` bridge into a produced object. It is the
    // input `attach_kdbg` converts while linking, and the bridge remains until
    // the debuggers consume KDBG directly (ADR 0008, ADR 0009).
    bool debug = true;
    // 0 or 1, matching `-O0` and `-O1`. No other level exists.
    int optimize = 0;
    Stage stage = Stage::Assembly;
    // Output assembly, IR or AST text on the result when the stage reaches it.
    // They are returned as data; the facade never prints.
    bool dump_ast = false;
    bool dump_ir = false;
    bool dump_stats = false;
    // 0 selects the target's default load address. `link` infers the target from
    // the objects themselves; the inferred default is used when this is 0.
    std::uint64_t base = 0;
    // Entry symbol. Empty selects "main". `build` and `link` require the symbol
    // to be defined unless the object set carries no entry at all.
    std::string entry;
    // Directories searched for `#include`, in order, after the including file's
    // own directory for a quoted include.
    std::vector<std::string> include_paths;
    // Preprocessor definitions, each `NAME` or `NAME=VALUE`, applied after the
    // target's predefined macros.
    std::vector<std::string> defines;
    // Applied by the linker's source-path rewrite before KDBG is built. Empty
    // leaves the recorded paths unchanged.
    std::string source_root;
    // The translation unit's own name, recorded in the debug bridge so a debugger
    // can open the file. Empty records "assembly", which is what the kr232
    // assembler recorded for every input before this existed.
    std::string source_name;
    // Emit calls in the kr232 indirect form, for an object whose calls can leave it.
    bool far_calls = false;
    // Drop sections the entry symbol cannot reach (section granularity). A
    // `.text.<name>` function section with no reference from a reachable
    // section is removed before layout.
    bool gc_sections = false;
    // Emit the sibling symbol sidecar as well as the container: a kr32 OS reads
    // its own symbols from it, verifying the CRC of the flat image (symbols.h).
    bool symbol_sidecar = false;
    // Section placements for the layout the host wants; empty uses the linker's
    // default packing.
    std::vector<SectionPlacement> section_addresses;
};

// One annotated statement of the assembly a compile produced.
//
// `section` names the section the statement belongs to. `address` is the
// statement's offset within that section for `assemble`, and its final address
// in the linked image for `build`. `bytes` are the encoded bytes the statement
// produced (empty for a label or a directive that allocates nothing). `text` is
// the statement as written, and `source_line` is the source line a preceding
// `.Ldbg` label attributed it to, or 0 when none did.
//
// A host renders its own `.lst`: the two VM front ends disagree on the format
// and keep their own.
struct ListingRow {
    std::string section;
    std::uint64_t address = 0;
    std::vector<std::uint8_t> bytes;
    std::string text;
    std::uint32_t source_line = 0;
};

// A diagnostic rendered to data. `file`, `line`, `column` and `line_text` are
// resolved while the source manager that produced them is still alive, so a
// host can render the diagnostic after the call has returned.
struct Diagnostic {
    frontend::Severity severity = frontend::Severity::Error;
    std::string message;
    std::string file;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string line_text;
    std::vector<std::string> notes;
};

struct CompileResult {
    bool ok = false;
    std::string assembly;
    // The textual bridge for the same translation unit. Both VM backends produce
    // it; it is empty for KR64, or when `Options::debug` is false.
    std::string debug_info;
    std::string ast_dump;
    std::string ir_dump;
    std::string stats;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

struct PreprocessResult {
    bool ok = false;
    std::vector<frontend::Token> tokens;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

struct AssembleResult {
    bool ok = false;
    std::vector<std::uint8_t> object;
    std::vector<ListingRow> listing;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

struct SectionInfo {
    std::string name;
    std::uint64_t address = 0;
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    // The section's contents, for sections that occupy memory. Empty for a
    // section that exists only in the object file, such as debug information,
    // which is what tells `flatten` which sections to place.
    std::vector<std::uint8_t> bytes;
};

struct SymbolInfo {
    std::string name;
    std::uint64_t value = 0;
    std::uint64_t size = 0;
    bool global = false;
};

struct LinkResult {
    bool ok = false;
    std::vector<std::uint8_t> image;
    // Filled only when Options::symbol_sidecar asked for it, and only for kr32:
    // a container carrying the image sections plus `__sym_crc`.
    std::vector<std::uint8_t> sidecar;
    std::vector<SectionInfo> sections;
    std::vector<SymbolInfo> symbols;
    std::uint64_t entry = 0;
    std::uint64_t base = 0;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

struct BuildResult {
    bool ok = false;
    std::vector<std::uint8_t> image;
    std::vector<ListingRow> listing;
    std::vector<SectionInfo> sections;
    std::vector<SymbolInfo> symbols;
    std::uint64_t entry = 0;
    std::uint64_t base = 0;
    std::vector<Diagnostic> diagnostics;
    std::string error;
};

[[nodiscard]] CompileResult compile(const Source& source, const Options& options);

// The link result as a flat image: each section that occupies memory copied to
// its linked address, zero everywhere else, spanning `base` to the end of the
// last such section. This is the form a boot loader reads; `LinkResult::image`
// is the container form, which carries headers and is what `krc` writes by
// default. A result with no allocated section flattens to nothing.
[[nodiscard]] std::vector<std::uint8_t> flatten(const LinkResult& linked);

// The deterministic image map: base and entry, then every section and symbol
// with its address and size. Stable text, so a committed map is comparable
// between hosts and between builds.
[[nodiscard]] std::string map_text(const LinkResult& linked);

// Preprocesses one translation unit and returns its tokens. `-E` is the only
// client; every other stage starts from `compile`.
[[nodiscard]] PreprocessResult preprocess(const Source& source, const Options& options);

// Assembles assembly text into a relocatable object. The overload without
// `debug_info` produces an object with no `.debug_info` bridge.
[[nodiscard]] AssembleResult assemble(std::string_view assembly,
                                      const Options& options);
[[nodiscard]] AssembleResult assemble(std::string_view assembly,
                                      std::string_view debug_info,
                                      const Options& options);

// Links object bytes into the target's executable container: a `.k32` image for
// KR32 and a `.kex` image for KR16. The target and the base default are inferred
// from the objects; `options.target` does not override the objects.
[[nodiscard]] LinkResult link(std::span<const Object> objects,
                              const Options& options);

// Compiles and assembles every source, links the resulting objects and returns
// the image plus the listing. Sources are linked in the order given. No runtime
// is added implicitly: a host that needs one links it with `link`.
[[nodiscard]] BuildResult build(std::span<const Source> sources,
                                const Options& options);

// Renders diagnostics in the compiler's canonical text form: located
// diagnostics name `file:line:column`, quote the source line and mark the
// column, and notes follow their diagnostic. The text ends with a newline when
// it is non-empty.
[[nodiscard]] std::string format_diagnostics(const std::vector<Diagnostic>& diagnostics);

[[nodiscard]] bool has_errors(const std::vector<Diagnostic>& diagnostics);

}  // namespace krc::toolchain
