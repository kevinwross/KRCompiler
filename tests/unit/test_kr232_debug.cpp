#include <doctest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
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

TEST_CASE("kr232 F and G records are unique for internal-linkage symbols in two units") {
    const std::string_view unit_a =
        "static int counter = 3;\n"
        "static int helper(int value) {\n"
        "    int local = value + counter;\n"
        "    return local;\n"
        "}\n"
        "int alpha(int seed) {\n"
        "    return helper(seed);\n"
        "}\n";
    const std::string_view unit_b =
        "int alpha(int seed);\n"
        "static int counter = 4;\n"
        "static int helper(int value) {\n"
        "    int local = value + counter;\n"
        "    return local;\n"
        "}\n"
        "int beta(int seed) {\n"
        "    return helper(seed);\n"
        "}\n"
        "int main(void) {\n"
        "    return beta(alpha(1));\n"
        "}\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.optimize = 0;
    std::vector<krc::toolchain::Source> units;
    units.push_back(source("unit_a.c", unit_a));
    units.push_back(source("unit_b.c", unit_b));
    const auto built = krc::toolchain::build(units, options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // Every lowered function has a record and a well-formed range, and the
    // ranges of distinct functions do not overlap or run backwards.
    REQUIRE(!image.functions.empty());
    for (const auto& function : image.functions) {
        CHECK_MESSAGE(function.start < function.end, function.name);
    }
    std::vector<const krc::debug::KdbgFunction*> sorted;
    for (const auto& function : image.functions) {
        sorted.push_back(&function);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const krc::debug::KdbgFunction* left,
                 const krc::debug::KdbgFunction* right) {
                  return left->start < right->start;
              });
    for (std::size_t index = 1; index < sorted.size(); ++index) {
        CHECK_MESSAGE(sorted[index - 1]->end <= sorted[index]->start,
                      sorted[index - 1]->name << " and " << sorted[index]->name);
    }

    // The two static helpers are two real bodies, not one range reported twice:
    // the bridge labels are per object, so the linker cannot resolve the second
    // unit's record to the first unit's label.
    std::vector<const krc::debug::KdbgFunction*> helpers;
    for (const auto& function : image.functions) {
        if (function.name == "helper") {
            helpers.push_back(&function);
        }
    }
    REQUIRE_EQ(helpers.size(), static_cast<std::size_t>(2));
    CHECK(helpers[0]->start != helpers[1]->start);
    CHECK(helpers[0]->end != helpers[1]->end);

    // The two static globals keep the source name `counter` and two distinct
    // addresses, so a debugger lists both rather than one address twice.
    std::vector<const krc::debug::KdbgSymbol*> counters;
    for (const auto& symbol : image.symbols) {
        if (symbol.name == "counter") {
            counters.push_back(&symbol);
        }
    }
    REQUIRE_EQ(counters.size(), static_cast<std::size_t>(2));
    CHECK(counters[0]->address != counters[1]->address);
    for (const krc::debug::KdbgSymbol* counter : counters) {
        CHECK_EQ(counter->size, 4u);
    }

    // A duplicated body's V/W rows are recorded once, not once per copy.
    std::map<std::pair<std::string, std::string>, int> rows;
    for (const auto& variable : image.variables) {
        ++rows[{variable.function, variable.name}];
    }
    for (const auto& [key, count] : rows) {
        CHECK_EQ(count, 1);
    }
    // Every line record lands inside a function; none is discarded because an
    // internal function had no F record.
    for (const auto& line : image.lines) {
        const bool inside =
            std::any_of(image.functions.begin(), image.functions.end(),
                        [&line](const krc::debug::KdbgFunction& function) {
                            return line.address >= function.start &&
                                   line.address < function.end;
                        });
        CHECK_MESSAGE(inside, "line at 0x" << std::hex << line.address);
    }
}

TEST_CASE("kr232 F carries the display name while K/V name the symbol") {
    const std::string_view text =
        "struct Widget {\n"
        "    int value;\n"
        "    int get(int k) const { int tmp = value + k; return tmp; }\n"
        "};\n"
        "struct Box { int x; Box(int v) { x = v; } };\n"
        "Box g_box(1);\n"
        "static int g_counter = 2;\n"
        "static int helper(int v) { int doubled = v * 2; return doubled; }\n"
        "int use(void) {\n"
        "    Widget w;\n"
        "    w.value = 5;\n"
        "    int out = w.get(3);\n"
        "    return helper(out) + w.value + g_counter;\n"
        "}\n"
        "int main(void) { return use() + g_box.x; }\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.cxx = true;
    options.optimize = 0;
    const auto built = krc::toolchain::build(one_unit("display.cpp", text), options);
    REQUIRE_MESSAGE(built.ok, built.error);
    const krc::debug::KdbgImage image = decode(built.image);

    // A member function is displayed by its source name but resolved by its
    // symbol; K and V name the symbol, F names the display name.
    const krc::debug::KdbgFunction* widget_get = find_function(image, "Widget::get");
    REQUIRE(widget_get != nullptr);
    CHECK(find_function(image, "Widget_get") == nullptr);
    bool saw_widget_scope = false;
    for (const auto& scope : image.scopes) {
        if (scope.function == "Widget_get" && scope.start == widget_get->start) {
            saw_widget_scope = true;
            CHECK_EQ(scope.name, std::string("Widget_get"));
        }
    }
    CHECK(saw_widget_scope);
    const krc::debug::KdbgVarInfo* this_info =
        find_varinfo(image, "Widget_get", "this");
    REQUIRE(this_info != nullptr);
    CHECK(this_info->kind == krc::debug::KdbgVariableKind::Parameter);

    // Static functions and static globals are present; the function is internal
    // linkage but still has an F record, and the global keeps its source name.
    CHECK(find_function(image, "helper") != nullptr);
    bool saw_counter = false;
    for (const auto& symbol : image.symbols) {
        saw_counter = saw_counter || symbol.name == "g_counter";
    }
    CHECK(saw_counter);

    // `main` is renamed to `__krc_user_main` when a runtime global initializer
    // needs a wrapper; the display name is still `main`, and the K identity is
    // the symbol the V records use.
    const krc::debug::KdbgScope* user_main_scope = nullptr;
    for (const auto& scope : image.scopes) {
        if (scope.function == "__krc_user_main") {
            user_main_scope = &scope;
        }
    }
    REQUIRE(user_main_scope != nullptr);
    bool saw_user_main_display = false;
    for (const auto& function : image.functions) {
        if (function.name == "main" && function.start == user_main_scope->start) {
            saw_user_main_display = true;
        }
    }
    CHECK(saw_user_main_display);
    // The global-initializer wrapper has no source definition but is still a
    // function with a record, so a stop inside it names something.
    CHECK(find_function(image, "__krc_init_globals") != nullptr);
}

TEST_CASE("kr232 .debug_function describes a hand-written internal function") {
    // `hidden` is internal linkage: the assembler must take the F record from the
    // directive, not from the `.globl` heuristic (it is not exported), and it
    // must label the range itself so another object's `hidden` cannot collide.
    const std::string assembly =
        ".text\n"
        ".globl entry_start\n"
        "entry_start:\n"
        "  call hidden\n"
        "  ret\n"
        "hidden:\n"
        "  enter #8, #0\n"
        "  ld r2, #1\n"
        "  leave #0\n"
        "  ret\n"
        ".Lhidden.end:\n"
        ".debug_function display_hidden hidden 8 i32\n"
        ".debug_scope hidden hidden - hidden .Lhidden.end\n";
    krc::toolchain::Options options;
    options.target = krc::toolchain::Target::Kr232;
    options.entry = "entry_start";
    options.source_name = "hand.s";
    const auto assembled = krc::toolchain::assemble(assembly, options);
    REQUIRE_MESSAGE(assembled.ok, assembled.error);
    std::vector<krc::toolchain::Object> objects(1);
    objects[0].name = "hand.o";
    objects[0].bytes = assembled.object;
    const auto linked = krc::toolchain::link(objects, options);
    REQUIRE_MESSAGE(linked.ok, linked.error);
    const krc::debug::KdbgImage image = decode(linked.image);

    REQUIRE_EQ(image.functions.size(), static_cast<std::size_t>(1));
    const krc::debug::KdbgFunction& function = image.functions.front();
    CHECK_EQ(function.name, std::string("display_hidden"));
    CHECK_EQ(function.frame, 8u);
    CHECK_EQ(function.type, std::string("i32"));
    CHECK(function.start < function.end);
    REQUIRE_EQ(image.scopes.size(), static_cast<std::size_t>(1));
    CHECK_EQ(image.scopes.front().function, std::string("hidden"));
    CHECK_EQ(image.scopes.front().start, function.start);
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
