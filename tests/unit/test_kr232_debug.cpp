#include <doctest.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "asm/kr232_assembler.h"
#include "krc/debug/kdbg.h"
#include "krc/toolchain.h"
#include "linker/kro_object.h"

namespace {

std::uint32_t read_u32(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint32_t>(bytes[at]) |
           (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
}

std::uint16_t read_u16(const std::vector<std::uint8_t>& bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] | (bytes[at + 1] << 8));
}

krc::toolchain::Source source(std::string name, std::string_view text) {
    return krc::toolchain::Source{std::move(name), text};
}

// The `.kdbg` payload inside a linked container. The container's section table
// is what the debugger reads, so this is the same path krdbg takes.
std::vector<std::uint8_t> kdbg_payload(const std::vector<std::uint8_t>& container) {
    const std::uint16_t count = read_u16(container, 22);
    const std::uint32_t table = read_u32(container, 32);
    const std::uint32_t strings = read_u32(container, 36);
    for (std::uint16_t index = 0; index < count; ++index) {
        const std::uint32_t at = table + 32u * index;
        const std::string name(reinterpret_cast<const char*>(
            container.data() + strings + read_u32(container, at)));
        if (name != ".kdbg") {
            continue;
        }
        const std::uint32_t offset = read_u32(container, at + 12);
        const std::uint32_t size = read_u32(container, at + 16);
        return std::vector<std::uint8_t>(container.begin() + offset,
                                         container.begin() + offset + size);
    }
    return {};
}

krc::debug::KdbgImage decode(const std::vector<std::uint8_t>& container) {
    const std::vector<std::uint8_t> payload = kdbg_payload(container);
    REQUIRE(!payload.empty());
    krc::debug::KdbgImage image;
    std::string error;
    REQUIRE_MESSAGE(krc::debug::decode_kdbg(payload, image, error), error);
    return image;
}

bool has_line(const krc::debug::KdbgImage& image, std::uint64_t address,
              std::uint32_t line) {
    for (const auto& record : image.lines) {
        if (record.address == address && record.line == line) {
            return true;
        }
    }
    return false;
}

const krc::debug::KdbgFunction* find_function(const krc::debug::KdbgImage& image,
                                              const std::string& name) {
    for (const auto& function : image.functions) {
        if (function.name == name) {
            return &function;
        }
    }
    return nullptr;
}

const krc::debug::KdbgVarInfo* find_varinfo(const krc::debug::KdbgImage& image,
                                            const std::string& function,
                                            const std::string& name) {
    for (const auto& info : image.variable_info) {
        if (info.function == function && info.name == name) {
            return &info;
        }
    }
    return nullptr;
}

std::vector<krc::toolchain::Source> one_unit(std::string name, std::string_view text) {
    std::vector<krc::toolchain::Source> units;
    units.push_back(source(std::move(name), text));
    return units;
}

}  // namespace

TEST_CASE("kr232 .debug_line drives L records and 0 suppresses them") {
    const std::string assembly =
        ".globl main\n"
        ".text\n"
        "main:\n"
        "  ld r2, #1\n"
        ".debug_line 7\n"
        "  ld r2, #2\n"
        ".debug_line 0\n"
        "  ld r2, #3\n"
        ".debug_line other.cpp 42\n"
        "  ld r2, #4\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.source_name = "fixture.s";
    const auto assembled = krc::toolchain::assemble(assembly, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    std::vector<krc::toolchain::Object> objects(1);
    objects[0].name = "fixture.o";
    objects[0].bytes = assembled.object;
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);
    const krc::debug::KdbgImage image = decode(linked.image);

    // Three records: statement 1 falls back to its assembly line, statement 2
    // takes line 7, the `.debug_line 0` statement has no record, and the fourth
    // names the other file's line 42.
    REQUIRE_EQ(image.lines.size(), static_cast<std::size_t>(3));
    bool saw_line_7 = false;
    bool saw_line_42 = false;
    bool saw_line_6 = false;
    for (const auto& record : image.lines) {
        saw_line_7 = saw_line_7 || record.line == 7;
        saw_line_42 = saw_line_42 || record.line == 42;
        saw_line_6 = saw_line_6 || record.line == 6;
    }
    CHECK(saw_line_7);
    CHECK(saw_line_42);
    // The suppressed instruction sat between other.cpp's line-42 statement and
    // main's `ld r2, #1`; no assembly line 6 may leak into the table.
    CHECK_FALSE(saw_line_6);
    bool saw_other_source = false;
    for (const auto& file : image.sources) {
        saw_other_source = saw_other_source || file.path == "other.cpp";
    }
    CHECK(saw_other_source);
}

TEST_CASE("kr232 W and K directives reach the KDBG payload") {
    const std::string assembly =
        ".globl main\n"
        ".text\n"
        "main:\n"
        "  enter #8, #0\n"
        ".debug_line 3\n"
        ".debug_var main value -4 i32 4\n"
        ".debug_varinfo main value local main 0x0\n"
        ".debug_scope main main - main .Lmain.end\n"
        "  st [r13-4], r2\n"
        "  leave #0\n"
        "  ret\n"
        ".Lmain.end:\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.source_name = "fixture.s";
    const auto assembled = krc::toolchain::assemble(assembly, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    std::vector<krc::toolchain::Object> objects(1);
    objects[0].name = "fixture.o";
    objects[0].bytes = assembled.object;
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);
    const krc::debug::KdbgImage image = decode(linked.image);

    REQUIRE_EQ(image.variables.size(), static_cast<std::size_t>(1));
    CHECK_EQ(image.variables[0].function, std::string("main"));
    CHECK_EQ(image.variables[0].name, std::string("value"));
    const krc::debug::KdbgVarInfo* info = find_varinfo(image, "main", "value");
    REQUIRE(info != nullptr);
    CHECK(info->kind == krc::debug::KdbgVariableKind::Local);
    CHECK_EQ(info->scope, std::string("main"));
    REQUIRE_EQ(image.scopes.size(), static_cast<std::size_t>(1));
    CHECK_EQ(image.scopes[0].function, std::string("main"));
    CHECK_EQ(image.scopes[0].name, std::string("main"));
    CHECK(image.scopes[0].start < image.scopes[0].end);
}

TEST_CASE("kr232 C++ symbols name real lines, params and locals") {
    const std::string_view text =
        "struct P { int x; P(int v) { x = v; } int get() const { return x; } };\n"
        "int compute(int seed) {\n"
        "    int x = seed;\n"
        "    {\n"
        "        int x = seed + 1;\n"
        "        x = x + 1;\n"
        "        seed = x;\n"
        "    }\n"
        "    int total = P(seed).get();\n"
        "    return total;\n"
        "}\n"
        "int main() {\n"
        "    return compute(2);\n"
        "}\n";
    constexpr std::uint32_t kSourceLines = 14;
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.cxx = true;
    const auto built =
        krc::toolchain::build(one_unit("symbols.cpp", text), options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // Every line record names a line that exists in the source: this is the
    // invariant that catches a generated-assembly line leaking into a .cpp.
    REQUIRE(!image.lines.empty());
    for (const auto& record : image.lines) {
        CHECK(record.line >= 1);
        CHECK(record.line <= kSourceLines);
    }
    bool saw_symbols_source = false;
    for (const auto& file : image.sources) {
        saw_symbols_source = saw_symbols_source || file.path == "symbols.cpp";
    }
    CHECK(saw_symbols_source);

    // The prologue and the definition share the function's entry address, so the
    // entry record names the definition line (line 2), not an assembly line.
    const krc::debug::KdbgFunction* compute = find_function(image, "compute");
    REQUIRE(compute != nullptr);
    CHECK(has_line(image, compute->start, 2));

    // Kinds: the parameter is a param, the two same-named declarations are
    // locals, and the shadowed one is recorded under its unique name.
    const krc::debug::KdbgVarInfo* seed = find_varinfo(image, "compute", "seed");
    REQUIRE(seed != nullptr);
    CHECK(seed->kind == krc::debug::KdbgVariableKind::Parameter);
    const krc::debug::KdbgVarInfo* outer = find_varinfo(image, "compute", "x");
    REQUIRE(outer != nullptr);
    CHECK(outer->kind == krc::debug::KdbgVariableKind::Local);
    const krc::debug::KdbgVarInfo* inner = find_varinfo(image, "compute", "x.2");
    REQUIRE(inner != nullptr);
    CHECK(inner->kind == krc::debug::KdbgVariableKind::Local);
    CHECK(find_varinfo(image, "compute", "total") != nullptr);

    // Compiler temporaries are not source variables and must not be listed.
    for (const auto& variable : image.variables) {
        CHECK(variable.name.find("temporary") == std::string::npos);
        CHECK(variable.name.find(".temp") == std::string::npos);
        CHECK(variable.name.find("argument.copy") == std::string::npos);
        CHECK(variable.name.find("cond.tmp") == std::string::npos);
    }
}

TEST_CASE("kr232 keeps named locals at -O2 (ADR 0052 policy)") {
    const std::string_view text =
        "int value(int seed) {\n"
        "    int doubled = seed + seed;\n"
        "    doubled = doubled + 1;\n"
        "    return doubled;\n"
        "}\n"
        "int main(void) {\n"
        "    return value(3);\n"
        "}\n";
    for (const int level : {0, 1, 2}) {
        krc::toolchain::Options options;
        options.target = krc::toolchain::Target::Kr232;
        options.optimize = level;
        const auto built =
            krc::toolchain::build(one_unit("promoted.c", text), options);
        REQUIRE_MESSAGE(built.ok, built.error);
        const krc::debug::KdbgImage image = decode(built.image);
        if (level < 2) {
            // The variable is a parameter or a local of `value`; inlining is not
            // allowed below -O2, so the function keeps its own records.
            const krc::debug::KdbgVarInfo* doubled =
                find_varinfo(image, "value", "doubled");
            REQUIRE_MESSAGE(doubled != nullptr, "-O" << level);
            CHECK(doubled->kind == krc::debug::KdbgVariableKind::Local);
        } else {
            // -O2 may inline `value` into main, but the named local must still be
            // listed somewhere: kr232 never promotes a recorded alloca.
            bool saw = false;
            for (const auto& variable : image.variables) {
                saw = saw || variable.name == "doubled";
            }
            CHECK(saw);
        }
    }
}

TEST_CASE("kr232 keeps a store to a recorded slot through the -O1 peepholes") {
    const std::string_view text =
        "int fixed(int a) {\n"
        "    int unused = a + 1;\n"
        "    return a;\n"
        "}\n"
        "int main(void) {\n"
        "    return fixed(3);\n"
        "}\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.optimize = 1;
    const auto compiled = krc::toolchain::compile(source("dead.c", text), options);
    REQUIRE_MESSAGE(compiled.ok, compiled.error);
    std::string_view assembly = compiled.assembly;

    const std::string marker = ".debug_var fixed unused -";
    const std::size_t at = assembly.find(marker);
    REQUIRE_MESSAGE(at != std::string_view::npos, "no unused record in the assembly");
    std::size_t digits = at + marker.size();
    std::string offset;
    while (digits < assembly.size() && assembly[digits] >= '0' && assembly[digits] <= '9') {
        offset.push_back(assembly[digits]);
        ++digits;
    }
    REQUIRE(!offset.empty());
    // The slot is FP-relative; the emitter stores at [r13-<offset>].
    const std::string store = "st [r13-" + offset + "]";
    CHECK_MESSAGE(assembly.find(store) != std::string_view::npos,
                  "the V-recorded slot has no store left: " << store);
}
