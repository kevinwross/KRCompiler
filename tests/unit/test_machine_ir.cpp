// The machine-IR printer, checked rather than asserted (R1, `docs/optimize.md` §7).
//
// The round-trip is the point: every instruction line the backend really emits must
// parse into the representation and print back to the same text, so that a lowering
// written against the representation cannot be silently lossy. The corpus tests at
// the bottom run over real emitted assembly rather than hand-written lines, because a
// hand-written set only covers the shapes I already thought of.

#include "doctest.h"

#include <string>
#include <string_view>
#include <filesystem>
#include <map>
#include <set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>

#include "backend/machine_ir.h"
#include "backend/kr32_backend.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "ir/ir.h"
#include "irgen/c_irgen.h"
#include "krc/target/target.h"

using namespace krc::backend::machine;

namespace {

// The C path of the front end, as `test_irgen.cpp` drives it. Duplicated rather than
// shared because it is three lines of wiring and a shared helper would need its own
// header for one caller.
std::unique_ptr<krc::ir::Module> generate(const std::string& source, bool cxx = false) {
    using namespace krc::frontend;
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer(cxx ? "round_trip.cpp" : "round_trip.c", std::string(source));
    const auto& profile = krc::target::get_target(krc::target::TargetId::Kr32);
    Preprocessor preprocessor(sources, diagnostics, profile);
    auto tokens = preprocessor.preprocess(file);
    Parser parser(std::move(tokens), diagnostics, sources, cxx);
    auto ast = parser.parse_translation_unit();
    if (!ast || parser.had_error() || diagnostics.has_errors()) {
        return nullptr;
    }
    Sema sema(diagnostics, profile, cxx);
    sema.analyze(*ast);
    if (diagnostics.has_errors()) {
        return nullptr;
    }
    krc::irgen::CIRGen irgen(sema, *ast, profile, diagnostics);
    auto module = irgen.generate();
    if (!module) {
        return nullptr;
    }
    krc::ir::Verifier verifier;
    std::string verify_error;
    if (!verifier.verify(*module, verify_error)) {
        return nullptr;
    }
    return module;
}

// Every non-empty line of an emitted assembly file, classified.
std::vector<std::string> instruction_lines(const std::string& assembly) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < assembly.size()) {
        const auto end = assembly.find('\n', start);
        const auto line = assembly.substr(start, end == std::string::npos ? std::string::npos
                                                                         : end - start);
        // An indent means "instruction or directive"; a directive is recognised by
        // its leading dot and is not part of the representation.
        if (line.size() > 8 && line.substr(0, 8) == "        ") {
            const auto first = line.substr(8).find_first_not_of(" \t");
            if (first != std::string::npos && line.substr(8)[first] != '.') {
                lines.push_back(line);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return lines;
}

}  // namespace

TEST_CASE("A machine instruction prints in the assembler's syntax") {
    Instruction store;
    store.opcode = Opcode::Store;
    store.operands = {Operand::physical(0), Operand::frame_offset(12)};
    CHECK(print_instruction(store) == "        STORE R0, [R7 + 12]\n");

    Instruction load;
    load.opcode = Opcode::Load;
    load.operands = {Operand::physical(1), Operand::frame_offset(68)};
    CHECK(print_instruction(load) == "        LOAD R1, [R7 + 68]\n");

    // A negative displacement keeps the sign inside the bracket, which is how the
    // backend prints the outgoing-argument area below the frame base.
    Instruction below;
    below.opcode = Opcode::Load;
    below.operands = {Operand::physical(0), Operand::frame_offset(-4)};
    CHECK(print_instruction(below) == "        LOAD R0, [R7 - 4]\n");
}

TEST_CASE("Immediates, symbols and comments print as the backend prints them") {
    Instruction adjust;
    adjust.opcode = Opcode::AdjustStack;
    adjust.operands = {Operand::immediate(-104)};
    CHECK(print_instruction(adjust) == "        ADJSP #-104\n");

    Instruction add;
    add.opcode = Opcode::Add;
    add.operands = {Operand::physical(0), Operand::physical(0), Operand::immediate(4)};
    CHECK(print_instruction(add) == "        ADD R0, R0, #4\n");

    Instruction call;
    call.opcode = Opcode::Call;
    call.operands = {Operand::named("memcpy")};
    CHECK(print_instruction(call) == "        CALL memcpy\n");

    Instruction commented;
    commented.opcode = Opcode::Halt;
    commented.comment = "unsupported";
    CHECK(print_instruction(commented) == "        HALT ; unsupported\n");
}

TEST_CASE("A function prints its symbol, labels and instructions in order") {
    Function function;
    function.symbol = "sum5";
    Block entry;
    entry.label = ".Lsum5.entry";
    Instruction push;
    push.opcode = Opcode::Push;
    push.operands = {Operand::physical(7)};
    entry.instructions.push_back(push);
    Instruction adjust;
    adjust.opcode = Opcode::AdjustStack;
    adjust.operands = {Operand::immediate(-104)};
    entry.instructions.push_back(adjust);
    function.blocks.push_back(entry);

    CHECK(print_function(function) ==
          "sum5:\n"
          ".Lsum5.entry:\n"
          "        PUSH R7\n"
          "        ADJSP #-104\n");
}

TEST_CASE("A virtual register prints distinctly from a physical one") {
    Instruction move;
    move.opcode = Opcode::Mov;
    move.operands = {Operand::physical(0), Operand::virtual_register(3)};
    CHECK(print_instruction(move) == "        MOV R0, %v3\n");
}

TEST_CASE("Parsing a printed instruction reproduces it") {
    for (const auto* text : {"        STORE R0, [R7 + 12]\n",
                             "        LOAD R1, [R7 + 68]\n",
                             "        LOAD R0, [R7 - 4]\n",
                             "        ADJSP #-104\n",
                             "        ADJSP #16\n",
                             "        ADD R0, R0, #4\n",
                             "        MOV R0, R1\n",
                             "        MOV R0, #0\n",
                             "        CALL memcpy\n",
                             "        JMP .Lmain.exit\n",
                             "        HALT ; unsupported\n",
                             "        RET\n"}) {
        const auto parsed = parse_instruction(std::string_view(text).substr(0, std::string_view(text).size() - 1));
        REQUIRE(parsed.has_value());
        CHECK(print_instruction(*parsed) == text);
    }
}

TEST_CASE("A label or a directive is not an instruction") {
    CHECK_FALSE(parse_instruction(".Lsum5.entry:").has_value());
    CHECK_FALSE(parse_instruction("        .word 48").has_value());
    CHECK_FALSE(parse_instruction("").has_value());
}

TEST_CASE("The round-trip covers the shapes only a richer program produces") {
    // Floats, byte accesses, a call and a switch: the opcodes the first sample never
    // reaches (`FLOAD.D`, `LOAD8S`, `STORE8`, `CALL`, `RET`, `PUSH`, `POP`, `HALT`).
    const auto source = std::string(R"SRC(
extern int putchar(int);
double scale(double value, double factor) { return value * factor; }
int classify(char *bytes, int count) {
    int total = 0;
    for (int index = 0; index < count; index = index + 1) {
        switch (bytes[index]) {
        case 'a': total = total + 1; break;
        case 'b': total = total + 2; break;
        default: putchar(bytes[index]); break;
        }
    }
    return total;
}
)SRC");
    const auto module = generate(source);
    REQUIRE(module);

    krc::backend::Kr32Backend backend(*module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    const auto lines = instruction_lines(*assembly);
    REQUIRE(lines.size() > 30);
    std::size_t unmatched = 0;
    std::string first_unmatched;
    for (const auto& line : lines) {
        const auto parsed = parse_instruction(line);
        const bool matches = parsed && print_instruction(*parsed) == line + "\n";
        if (!matches) {
            if (unmatched == 0) {
                first_unmatched = line;
            }
            ++unmatched;
        }
    }
    INFO("first unmatched: " << first_unmatched);
    CHECK(unmatched == 0);
}
namespace {

std::string printed_mnemonic(const Instruction& instruction) {
    const auto text = print_instruction(instruction);
    const auto start = text.find_first_not_of(" ");
    const auto end = text.find(" ", start);
    return text.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

}  // namespace

TEST_CASE("The prologue conversion records the same instructions it prints") {
    // The first converted emit site. The recording exists so the text and the machine
    // form cannot disagree: one call site produces both, and this holds the first one
    // to that.
    krc::ir::Module module{krc::target::TargetId::Kr32};
    const auto i32 = module.types().int_type(32);
    const auto type = module.types().function_type(i32, std::vector<krc::ir::TypePtr>{i32});
    auto* function = module.add_function("answer", type, krc::ir::Linkage::External);
    auto* argument = function->add_parameter(i32, "argument");
    auto* entry = function->add_block("entry");

    krc::ir::IRBuilder builder(module.types());
    builder.set_insert_point(entry);
    builder.create_ret(argument);

    krc::backend::Kr32Backend backend(module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    const auto* recorded = backend.machine_function("answer");
    REQUIRE(recorded != nullptr);
    REQUIRE_FALSE(recorded->blocks.empty());
    const auto& instructions = recorded->blocks.front().instructions;
    REQUIRE(instructions.size() >= 3);

    CHECK(printed_mnemonic(instructions[0]) == "PUSH");
    CHECK(printed_mnemonic(instructions[1]) == "ADJSP");
    CHECK(printed_mnemonic(instructions[2]) == "LEA");

    // The recorded sequence is a prefix of the instruction sequence that was
    // emitted. Compared as sequences, not as a substring: labels and comments sit
    // between instructions in the text, and the recording deliberately does not
    // carry them.
    // Every recorded instruction appears in the emitted text, in the same order. A
    // subsequence rather than a prefix: sites converted later (the parameter stores,
    // the epilogue) still emit raw text, so the recording skips them.
    const auto emitted = instruction_lines(*assembly);
    std::size_t position = 0;
    std::size_t missing = 0;
    for (const auto& instruction : instructions) {
        const auto wanted = print_instruction(instruction);
        while (position < emitted.size() && emitted[position] + "\n" != wanted) {
            ++position;
        }
        if (position == emitted.size()) {
            ++missing;
        } else {
            ++position;
        }
    }
    CHECK(missing == 0);

    // How much of the function the recording covers. This number is the remaining
    // work in the conversion, so it is asserted rather than admired: a site that was
    // converted raises it, and one that regresses lowers it.
    const auto recorded_count = instructions.size();
    const auto emitted_count = emitted.size();
    INFO("recorded " << recorded_count << " of " << emitted_count << " instructions");
    CHECK(recorded_count * 10 >= emitted_count * 8);
}
TEST_CASE("The recording covers most of a realistic function") {
    // The conversion's progress meter. A simple function records completely; the
    // interesting number is what a realistic one records, because that is how much of
    // the emitter a machine-level allocation would actually be able to see.
    const auto source = std::string(R"SRC(
extern int putchar(int);
int accumulate(int *values, int count) {
    int total = 0;
    for (int index = 0; index < count; index = index + 1) {
        total = total + values[index];
    }
    return total;
}
int describe(int value, int other) {
    if (value < other) {
        return value - other;
    } else if (value == other) {
        putchar(value);
        return 0;
    }
    return value + other;
}
)SRC");
    const auto module = generate(source);
    REQUIRE(module);

    krc::backend::Kr32Backend backend(*module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    std::size_t recorded = 0;
    for (const auto& function : module->functions()) {
        if (!function || function->declaration) {
            continue;
        }
        const auto* machine_function = backend.machine_function(function->name);
        if (machine_function == nullptr) {
            continue;
        }
        std::size_t function_recorded = 0;
        for (const auto& block : machine_function->blocks) {
            function_recorded += block.instructions.size();
        }
        recorded += function_recorded;
        // Temporary diagnostic: which mnemonics of this function are unrecorded.
        const auto marker = "\n" + function->name + ":\n";
        const auto start = assembly->find(marker);
        if (start != std::string::npos) {
            auto end = assembly->find("\n        .text\n", start);
            if (end == std::string::npos) {
                end = assembly->size();
            }
            const auto span = assembly->substr(start, end - start);
            const auto lines = instruction_lines(span);
            std::size_t position = 0;
            std::string missing;
            for (const auto& line : lines) {
                bool found = false;
                for (const auto& block : machine_function->blocks) {
                    for (const auto& instruction : block.instructions) {
                        if (print_instruction(instruction) == line + "\n") {
                            found = true;
                            break;
                        }
                    }
                    if (found) {
                        break;
                    }
                }
                if (!found) {
                    missing += " " + line.substr(8, line.find(' ', 8) - 8);
                }
            }
            // Printed only when something is missing: the diagnostic is the list of
            // sites still emitting raw text, which is the remaining conversion work.
            if (!missing.empty()) {
                std::cerr << "[" << function->name << "] recorded " << function_recorded
                          << " of " << lines.size() << "; unrecorded:" << missing << "\n";
            }
            static_cast<void>(position);
        }
    }
    const auto emitted = instruction_lines(*assembly).size();
    INFO("recorded " << recorded << " of " << emitted << " instructions");
    REQUIRE(emitted > 40);
    // Complete for this program: every instruction of both functions is recorded, so
    // the machine form is the output rather than a description of it. A regression
    // here means a converted site was reverted.
    CHECK(recorded == emitted);
}
TEST_CASE("The recording covers the corpus, and says where it does not") {
    // The corpus-wide version of the coverage measurement. Every function is emitted,
    // its recorded instruction count compared with its emitted one, and the unrecorded
    // mnemonics are named: that list is the remaining conversion work, and it is the
    // instrument that worked on the sample.
    namespace fs = std::filesystem;
    const auto corpus = fs::path(KRC_SOURCE_DIR) / "tests" / "corpus" / "extracted";
    std::vector<fs::path> files;
    for (const auto* directory : {"tier1", "tier2"}) {
        for (const auto& entry : fs::directory_iterator(corpus / directory)) {
            const auto extension = entry.path().extension().string();
            if (extension == ".c" || extension == ".cpp") {
                files.push_back(entry.path());
            }
        }
    }
    REQUIRE(files.size() >= 20);

    std::size_t recorded_total = 0;
    std::size_t emitted_total = 0;
    std::map<std::string, std::size_t> unrecorded;
    for (const auto& path : files) {
        std::ifstream input(path);
        REQUIRE(input.good());
        std::stringstream buffer;
        buffer << input.rdbuf();
        const auto module = generate(buffer.str(), path.extension() == ".cpp");
        if (!module) {
            continue;
        }
        krc::backend::Kr32Backend backend(*module,
                                          krc::target::get_target(krc::target::TargetId::Kr32));
        std::string error;
        const auto assembly = backend.emit(error);
        REQUIRE(assembly.has_value());
        const auto lines = instruction_lines(*assembly);
        emitted_total += lines.size();
        for (const auto& function : module->functions()) {
            if (!function || function->declaration) {
                continue;
            }
            const auto* recorded = backend.machine_function(function->name);
            if (recorded == nullptr) {
                continue;
            }
            for (const auto& block : recorded->blocks) {
                recorded_total += block.instructions.size();
            }
            // Which of this function's lines the recording does not contain.
            std::set<std::string> have;
            for (const auto& block : recorded->blocks) {
                for (const auto& instruction : block.instructions) {
                    have.insert(print_instruction(instruction));
                }
            }
            const auto marker = "\n" + function->name + ":\n";
            const auto start = assembly->find(marker);
            if (start == std::string::npos) {
                continue;
            }
            auto end = assembly->find("\n        .text\n", start);
            if (end == std::string::npos) {
                end = assembly->size();
            }
            for (const auto& line : instruction_lines(assembly->substr(start, end - start))) {
                if (have.count(line + "\n") == 0) {
                    const auto space = line.find(' ', 8);
                    ++unrecorded[line.substr(8, space - 8)];
                }
            }
        }
    }
    std::string summary;
    for (const auto& [name, count] : unrecorded) {
        summary += " " + name + "=" + std::to_string(count);
    }
    std::cerr << "[corpus] recorded " << recorded_total << " of " << emitted_total
              << "; unrecorded:" << summary << "\n";
    REQUIRE(emitted_total > 5000);
    // A floor, raised as the conversion completes: the number in implementation.md is
    // the authority, this only stops a regression.
    CHECK(recorded_total * 4 >= emitted_total * 3);
}
TEST_CASE("Every instruction the compiler emits for the whole corpus round-trips") {
    // The corpus-wide claim, not a sample: if any file emits an instruction the
    // representation cannot express, this fails and names the line.
    namespace fs = std::filesystem;
    const auto corpus = fs::path(KRC_SOURCE_DIR) / "tests" / "corpus" / "extracted";
    std::vector<fs::path> files;
    for (const auto* directory : {"tier1", "tier2"}) {
        for (const auto& entry : fs::directory_iterator(corpus / directory)) {
            const auto extension = entry.path().extension().string();
            if (extension == ".c" || extension == ".cpp") {
                files.push_back(entry.path());
            }
        }
    }
    REQUIRE(files.size() >= 20);

    std::size_t checked_lines = 0;
    std::string first_unmatched;
    std::size_t unmatched = 0;
    for (const auto& path : files) {
        std::ifstream input(path);
        REQUIRE(input.good());
        std::stringstream buffer;
        buffer << input.rdbuf();
        const auto module = generate(buffer.str(), path.extension() == ".cpp");
        if (!module) {
            continue;  // a corpus file the front end rejects is another test's business
        }
        // Both levels: `-O1` emits different code (a register instead of a slot, so a
        // `MOV` where `-O0` had a `LOAD`/`STORE`), and a shape it alone produces would
        // otherwise escape the check.
        for (const auto level : {krc::backend::OptimizationLevel::O0,
                                 krc::backend::OptimizationLevel::O1}) {
            krc::backend::Kr32Backend backend(
                *module, krc::target::get_target(krc::target::TargetId::Kr32), level);
            std::string error;
            const auto assembly = backend.emit(error);
            REQUIRE(assembly.has_value());
            for (const auto& line : instruction_lines(*assembly)) {
                ++checked_lines;
                const auto parsed = parse_instruction(line);
                if (!parsed || print_instruction(*parsed) != line + "\n") {
                    if (unmatched == 0) {
                        first_unmatched = path.filename().string() + ": " + line;
                    }
                    ++unmatched;
                }
            }
        }
    }
    INFO("first unmatched: " << first_unmatched);
    CHECK(checked_lines > 10000);
    CHECK(unmatched == 0);
}
TEST_CASE("Every instruction the backend emits for the runtime round-trips") {
    // Real output, not a hand-written set: this is what proves the representation can
    // express the whole of what the emitter produces today.
    const auto source = std::string(R"SRC(
int accumulate(int *values, int count) {
    int total = 0;
    int index = 0;
    while (index < count) {
        total = total + values[index];
        index = index + 1;
    }
    return total;
}
int describe(int value, int other) {
    if (value < other) {
        return value - other;
    } else if (value == other) {
        return 0;
    }
    return value + other;
}
)SRC");
    const auto module = generate(source);
    REQUIRE(module);

    krc::backend::Kr32Backend backend(*module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    const auto lines = instruction_lines(*assembly);
    REQUIRE(lines.size() > 20);
    std::size_t unmatched = 0;
    std::string first_unmatched;
    for (const auto& line : lines) {
        const auto parsed = parse_instruction(line);
        if (!parsed) {
            if (unmatched == 0) {
                first_unmatched = line;
            }
            ++unmatched;
            continue;
        }
        if (print_instruction(*parsed) != line + "\n") {
            if (unmatched == 0) {
                first_unmatched = line;
            }
            ++unmatched;
        }
    }
    INFO("first unmatched: " << first_unmatched);
    CHECK(unmatched == 0);
}
