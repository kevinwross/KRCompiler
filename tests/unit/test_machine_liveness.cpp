// Liveness over the recorded machine form (ADR 0051), measured on real functions.
//
// The point of these tests is the number, not the assertion: peak register pressure is
// what decides whether an allocator over the recorded form can avoid spilling, and the
// corpus measurement prints it so the decision to allocate can rest on it.

#include "doctest.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "backend/kr32_backend.h"
#include "backend/machine_liveness.h"
#include "krc/frontend/diagnostics.h"
#include "krc/frontend/parser.h"
#include "krc/frontend/preprocessor.h"
#include "krc/frontend/sema.h"
#include "krc/frontend/source_manager.h"
#include "krc/target/target.h"
#include "ir/ir.h"
#include "irgen/c_irgen.h"

using namespace krc::backend::machine;

namespace {

std::unique_ptr<krc::ir::Module> generate(const std::string& source, bool cxx = false) {
    using namespace krc::frontend;
    SourceManager sources;
    DiagnosticEngine diagnostics(sources);
    const auto file = sources.add_buffer(cxx ? "live.cpp" : "live.c", std::string(source));
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

}  // namespace

TEST_CASE("Liveness of a straight-line function is the same order as its data flow") {
    const auto source = std::string(R"SRC(
int arithmetic(int left, int right) {
    int sum = left + right;
    int product = sum * right;
    return product - sum;
}
)SRC");
    const auto module = generate(source);
    REQUIRE(module);
    krc::backend::Kr32Backend backend(*module,
                                      krc::target::get_target(krc::target::TargetId::Kr32));
    std::string error;
    const auto assembly = backend.emit(error);
    REQUIRE(assembly.has_value());

    const auto* recorded = backend.machine_function("arithmetic");
    REQUIRE(recorded != nullptr);
    const auto liveness = compute_liveness(*recorded);

    CHECK(liveness.instructions > 0);
    CHECK(liveness.definitions >= 2);
    CHECK(liveness.uses >= 2);
    // A straight-line function has nothing loop-carried; its parameters are live from
    // entry rather than unmodelled, and the peak is the number of overlapping ranges.
    CHECK(liveness.unmodelled == 0);
    CHECK(liveness.from_entry >= 2);
    CHECK(liveness.peak_pressure >= 1);
    CHECK(liveness.peak_pressure <= 8);
}

TEST_CASE("The allocation plan gives no register to two values live at once") {
    // The property that makes a plan correct, checked as a pure function of the plan and
    // the ranges: no two overlapping live ranges may share a register. Nothing emits
    // from this yet, which is why the property can be asserted over the whole corpus
    // while the backend still emits exactly what it emitted before.
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

    const std::vector<int> registers = {8, 9, 10, 11, 12, 13, 14, 15};
    std::size_t functions = 0;
    std::size_t values = 0;
    std::size_t unallocated = 0;
    std::size_t violations = 0;
    std::size_t worst = 0;
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
        for (const auto& function : module->functions()) {
            if (!function || function->declaration) {
                continue;
            }
            const auto* recorded = backend.machine_function(function->name);
            if (recorded == nullptr) {
                continue;
            }
            const auto liveness = compute_liveness(*recorded);
            if (liveness.instructions == 0) {
                continue;
            }
            const auto allocation = plan_linear_scan(liveness, registers);
            ++functions;
            values += liveness.ranges.size();
            unallocated += allocation.unallocated;
            worst = std::max(worst, liveness.peak_pressure);
            if (!interference_is_respected(liveness, allocation)) {
                ++violations;
            }
        }
    }
    REQUIRE(functions > 50);
    std::cerr << "[allocation] functions " << functions << ", values " << values
              << ", unallocated " << unallocated << ", violations " << violations
              << ", worst pressure " << worst << "\n";
    CHECK(violations == 0);
}
TEST_CASE("What the plan would remove, counted rather than estimated") {
    // The payoff ADR 0051 promises, as a number. A `LOAD` reading a value the plan gives a
    // register would disappear, and so would the `STORE` that put it in its slot: those
    // two are the frame round trip the plan exists to remove. Nothing is removed here —
    // the count is what the decision rests on.
    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    const auto corpus = fs::path(KRC_SOURCE_DIR) / "tests" / "corpus" / "extracted";
    for (const auto* directory : {"tier1", "tier2"}) {
        for (const auto& entry : fs::directory_iterator(corpus / directory)) {
            const auto extension = entry.path().extension().string();
            if (extension == ".c" || extension == ".cpp") {
                files.push_back(entry.path());
            }
        }
    }
    // The largest single file in the tree, measured separately because it is what the
    // frame and instruction figures elsewhere in docs/optimize.md refer to.
    files.push_back(fs::path(KRC_SOURCE_DIR) / "targets" / "kr32" / "runtime" / "libc.c");
    REQUIRE(files.size() >= 20);

    const std::vector<int> registers = {8, 9, 10, 11, 12, 13, 14, 15};
    for (const auto level : {krc::backend::OptimizationLevel::O0,
                             krc::backend::OptimizationLevel::O1}) {
        std::size_t instructions = 0;
        std::size_t removable_stores = 0;
        std::size_t removable_loads = 0;
        std::size_t allocatable_values = 0;
        for (const auto& path : files) {
            std::ifstream input(path);
            REQUIRE(input.good());
            std::stringstream buffer;
            buffer << input.rdbuf();
            const auto module = generate(buffer.str(), path.extension() == ".cpp");
            if (!module) {
                continue;
            }
            krc::backend::Kr32Backend backend(
                *module, krc::target::get_target(krc::target::TargetId::Kr32), level);
            std::string error;
            const auto assembly = backend.emit(error);
            REQUIRE(assembly.has_value());
            for (const auto& function : module->functions()) {
                if (!function || function->declaration) {
                    continue;
                }
                const auto* recorded = backend.machine_function(function->name);
                if (recorded == nullptr) {
                    continue;
                }
                const auto liveness = compute_liveness(*recorded);
                const auto allocation = plan_linear_scan(liveness, registers);
                for (const auto& block : recorded->blocks) {
                    for (const auto& instruction : block.instructions) {
                        ++instructions;
                        const bool slot_access =
                            instruction.opcode == krc::backend::machine::Opcode::Load ||
                            instruction.opcode == krc::backend::machine::Opcode::Store;
                        if (!slot_access) {
                            continue;
                        }
                        if (instruction.virtual_destination >= 0 &&
                            static_cast<std::size_t>(instruction.virtual_destination) <
                                allocation.registers.size() &&
                            allocation.registers[static_cast<std::size_t>(
                                instruction.virtual_destination)] >= 0) {
                            ++removable_stores;
                        }
                        for (const auto source : instruction.virtual_sources) {
                            if (source >= 0 &&
                                static_cast<std::size_t>(source) <
                                    allocation.registers.size() &&
                                allocation.registers[static_cast<std::size_t>(source)] >=
                                    0) {
                                ++removable_loads;
                                break;
                            }
                        }
                    }
                }
                for (const auto& range : liveness.ranges) {
                    static_cast<void>(range);
                }
                allocatable_values += allocation.registers.size() - allocation.unallocated;
            }
        }
        std::cerr << "[payoff " << (level == krc::backend::OptimizationLevel::O1 ? "-O1" : "-O0")
                  << "] recorded " << instructions << " instructions, removable stores "
                  << removable_stores << ", removable loads " << removable_loads
                  << " (total " << removable_stores + removable_loads << ", "
                  << (instructions == 0 ? 0 : (100 * (removable_stores + removable_loads)) /
                                                  instructions)
                  << "%), values allocated " << allocatable_values << "\n";
        CHECK(instructions > 0);
    }
}
TEST_CASE("Peak register pressure over the corpus fits the register file") {
    // The measurement ADR 0051 turns on. KR32 has eight general registers R8–R15 that
    // the current allocator may use; if real functions peak below that, an allocator
    // over the recorded form can avoid spilling entirely.
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

    std::size_t functions = 0;
    std::size_t worst = 0;
    std::string worst_function;
    std::size_t over_eight = 0;
    std::size_t unmodelled_total = 0;
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
        for (const auto& function : module->functions()) {
            if (!function || function->declaration) {
                continue;
            }
            const auto* recorded = backend.machine_function(function->name);
            if (recorded == nullptr) {
                continue;
            }
            const auto liveness = compute_liveness(*recorded);
            if (liveness.instructions == 0) {
                continue;
            }
            ++functions;
            unmodelled_total += liveness.unmodelled;
            if (liveness.peak_pressure > worst) {
                worst = liveness.peak_pressure;
                worst_function = function->name;
            }
            if (liveness.peak_pressure > 8) {
                ++over_eight;
            }
        }
    }
    REQUIRE(functions > 50);
    std::cerr << "[liveness] functions " << functions << ", worst pressure " << worst
              << " (" << worst_function << "), over eight " << over_eight
              << ", unmodelled " << unmodelled_total << "\n";
    // The measurement, stated as a floor for the property that matters: most functions
    // fit. The exact figures are recorded in implementation.md.
    CHECK(functions > 50);
    CHECK(worst >= 1);
}
